/**
 * Orchestra Wireless LED Control — Node Firmware
 * Version 2.0.0
 *
 * Hardware : RF-Nano (ATmega328P + NRF24L01+)
 * LEDs     : WS2812B x10 per node
 *
 * Protocol : node_packet_v1  —  32-byte unicast radio frame
 *   [ dst_addr(1) | frame_id(1) | rgb[30] ]
 *
 * Radio CRC handles integrity — no software CRC needed.
 *
 * Libraries (arduino-cli lib install):
 *   FastLED
 *   RF24
 */

#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <FastLED.h>

// ============================================================
//  Identity
// ============================================================
#define APP_NAME    "Orchestra-Node"
#define APP_VERSION "2.0.0"

// ============================================================
//  Pin assignments  (RF-Nano: CE=D10, CSN=D9, SPI=D11-13)
// ============================================================
#define PIN_LED_DATA    A0  // WS2812B data — isolated on analog side

#define PIN_TEST_MODE   2   // jumper to GND → test mode

#define PIN_ADDR_BIT0   3   // address jumpers, active LOW + pull-up
#define PIN_ADDR_BIT1   4
#define PIN_ADDR_BIT2   5
#define PIN_ADDR_BIT3   6
#define PIN_ADDR_BIT4   7
#define PIN_ADDR_BIT5   8   // MSB — gives addresses 0-63

#define RF_CE_PIN       10
#define RF_CSN_PIN      9

// ============================================================
//  LED
// ============================================================
#define NUM_LEDS        10
#define LED_TYPE        WS2812B
#define COLOR_ORDER     GRB

// ============================================================
//  Radio
// ============================================================
static const uint64_t PIPE_ADDR      = 0xE8E8F0F0E1LL;
#define RADIO_CHANNEL                  76
#define RADIO_PAYLOAD_SIZE             32   // == sizeof(node_packet_v1)

// ============================================================
//  Packet structure  (must be exactly RADIO_PAYLOAD_SIZE bytes)
// ============================================================
struct __attribute__((packed)) node_packet_v1 {
    uint8_t dst_addr;   /* 1-63, 0 forbidden */
    uint8_t frame_id;   /* wrapping counter incremented by gateway */
    uint8_t rgb[30];    /* 10 LED × 3 bytes (R, G, B) */
};

static_assert(sizeof(node_packet_v1) == RADIO_PAYLOAD_SIZE,
              "node_packet_v1 must be exactly 32 bytes");

// ============================================================
//  Timing
// ============================================================
#define RX_TIMEOUT_MS   2000UL
#define STATS_INTERVAL  10000UL
#define TEST_CHASE_MS   100UL

// ============================================================
//  Globals
// ============================================================
RF24  radio(RF_CE_PIN, RF_CSN_PIN);
CRGB  leds[NUM_LEDS];

uint8_t  my_address  = 0;
bool     test_mode   = false;
bool     leds_on     = false;

unsigned long last_valid_rx  = 0;
unsigned long last_stats_ts  = 0;

// frame_id dedup state
uint8_t  last_frame_id = 0;
bool     first_frame   = true;

// ============================================================
//  Radio statistics
// ============================================================
struct RadioStats {
    uint32_t rx_total;       // all payloads pulled from radio FIFO
    uint32_t rx_accepted;    // new frames applied to LEDs
    uint32_t rx_duplicate;   // same frame_id as previous
    uint32_t rx_late;        // frame_id older than previous
    uint32_t rx_wrong_addr;  // dst_addr != my_address
    uint32_t frames_lost;    // estimated missed frames (frame_id gaps)
};

static RadioStats stats;

void stats_reset() {
    memset(&stats, 0, sizeof(stats));
}

void stats_print() {
    uint32_t total_for_us = stats.rx_accepted
                          + stats.rx_duplicate
                          + stats.rx_late;
    Serial.println(F("---- Radio stats (10 s) ----"));
    Serial.print(F("  rx_total       : ")); Serial.println(stats.rx_total);
    Serial.print(F("  rx_wrong_addr  : ")); Serial.println(stats.rx_wrong_addr);
    Serial.print(F("  rx_for_us      : ")); Serial.println(total_for_us);
    Serial.print(F("    accepted      : ")); Serial.println(stats.rx_accepted);
    Serial.print(F("    duplicate     : ")); Serial.println(stats.rx_duplicate);
    Serial.print(F("    late          : ")); Serial.println(stats.rx_late);
    Serial.print(F("  frames_lost    : ")); Serial.println(stats.frames_lost);
    Serial.println(F("----------------------------"));
}

// ============================================================
//  Jumper helpers
// ============================================================
uint8_t read_address() {
    uint8_t a = 0;
    if (!digitalRead(PIN_ADDR_BIT0)) a |= (1 << 0);
    if (!digitalRead(PIN_ADDR_BIT1)) a |= (1 << 1);
    if (!digitalRead(PIN_ADDR_BIT2)) a |= (1 << 2);
    if (!digitalRead(PIN_ADDR_BIT3)) a |= (1 << 3);
    if (!digitalRead(PIN_ADDR_BIT4)) a |= (1 << 4);
    if (!digitalRead(PIN_ADDR_BIT5)) a |= (1 << 5);
    return a;
}

bool read_test_mode() { return !digitalRead(PIN_TEST_MODE); }

// ============================================================
//  LED helpers
// ============================================================
void leds_blank() {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    leds_on = false;
}

void leds_apply(const uint8_t *rgb) {
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        leds[i].r = rgb[i * 3];
        leds[i].g = rgb[i * 3 + 1];
        leds[i].b = rgb[i * 3 + 2];
    }
    FastLED.show();
    leds_on = true;
}

void do_test_chase() {
    static uint8_t        pos = 0;
    static unsigned long  ts  = 0;
    if (millis() - ts < TEST_CHASE_MS) return;
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    leds[pos] = CRGB(0, 0, 255);
    FastLED.show();
    pos = (pos + 1) % NUM_LEDS;
    ts  = millis();
}

// ============================================================
//  Packet handler
// ============================================================
void handle_packet(const node_packet_v1 &pkt) {
    stats.rx_total++;

    if (pkt.dst_addr != my_address || my_address == 0) {
        stats.rx_wrong_addr++;
        return;
    }

    if (!first_frame) {
        int8_t diff = (int8_t)(pkt.frame_id - last_frame_id);

        if (diff == 0) {
            stats.rx_duplicate++;
            return;
        }
        if (diff < 0) {
            stats.rx_late++;
            return;
        }
        // diff > 1 → missed frames
        if (diff > 1) {
            stats.frames_lost += (uint32_t)(diff - 1);
        }
    }

    first_frame   = false;
    last_frame_id = pkt.frame_id;
    stats.rx_accepted++;
    last_valid_rx = millis();

    leds_apply(pkt.rgb);
}

// ============================================================
//  Radio init
// ============================================================
bool radio_init() {
    if (!radio.begin()) return false;
    radio.setPALevel(RF24_PA_LOW);
    radio.setDataRate(RF24_1MBPS);
    radio.setChannel(RADIO_CHANNEL);
    radio.setAutoAck(false);                  // broadcast, no ACK
    radio.setPayloadSize(RADIO_PAYLOAD_SIZE);
    radio.openReadingPipe(1, PIPE_ADDR);
    radio.startListening();
    return true;
}

// ============================================================
//  Setup
// ============================================================
void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println(F(APP_NAME " v" APP_VERSION));
    Serial.println(F("------------------------------"));

    const uint8_t input_pins[] = {
        PIN_TEST_MODE,
        PIN_ADDR_BIT0, PIN_ADDR_BIT1, PIN_ADDR_BIT2,
        PIN_ADDR_BIT3, PIN_ADDR_BIT4, PIN_ADDR_BIT5,
    };
    for (uint8_t i = 0; i < sizeof(input_pins); i++)
        pinMode(input_pins[i], INPUT_PULLUP);

    my_address = read_address();
    test_mode  = read_test_mode();

    Serial.print(F("Node address : ")); Serial.println(my_address);
    Serial.print(F("Test mode    : ")); Serial.println(test_mode ? F("ON") : F("OFF"));

    FastLED.addLeds<LED_TYPE, PIN_LED_DATA, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.setBrightness(200);
    leds_blank();

    if (!radio_init()) {
        Serial.println(F("[ERROR] Radio init failed — halting"));
        while (true) {
            fill_solid(leds, NUM_LEDS, CRGB::Red);
            FastLED.show();
            delay(300);
            leds_blank();
            delay(300);
        }
    }

    Serial.print(F("Radio OK — 1 Mbps, ch "));
    Serial.println(RADIO_CHANNEL);

    stats_reset();
    last_valid_rx = millis();
    last_stats_ts = millis();
}

// ============================================================
//  Main loop
// ============================================================
void loop() {
    // --- Re-read jumpers (immediate effect) ---
    uint8_t new_addr = read_address();
    bool    new_test = read_test_mode();

    if (new_addr != my_address) {
        my_address  = new_addr;
        first_frame = true;
        stats_reset();
        Serial.print(F("[INFO] Address -> ")); Serial.println(my_address);
        leds_blank();
    }

    if (new_test != test_mode) {
        test_mode = new_test;
        stats_reset();
        Serial.print(F("[INFO] Test mode -> "));
        Serial.println(test_mode ? F("ON") : F("OFF"));
        if (!test_mode) leds_blank();
    }

    // --- Test mode: chenillard, no radio processing ---
    if (test_mode) {
        do_test_chase();
        goto check_stats;
    }

    // --- Drain radio FIFO ---
    {
        uint8_t pipe;
        while (radio.available(&pipe)) {
            node_packet_v1 pkt;
            radio.read(&pkt, RADIO_PAYLOAD_SIZE);
            handle_packet(pkt);
        }
    }

    // --- RX timeout ---
    if (leds_on && (millis() - last_valid_rx > RX_TIMEOUT_MS)) {
        Serial.println(F("[INFO] RX timeout — blanking LEDs"));
        leds_blank();
    }

check_stats:
    // --- Stats every 10 s ---
    if (millis() - last_stats_ts >= STATS_INTERVAL) {
        stats_print();
        last_stats_ts = millis();
    }
}
