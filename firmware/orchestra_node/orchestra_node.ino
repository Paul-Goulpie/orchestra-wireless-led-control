/**
 * Orchestra Wireless LED Control - Node Firmware
 * Version 1.0.0
 *
 * Hardware: RF-Nano (ATmega328P + NRF24L01+)
 * LEDs:     WS2812B x10 per node
 *
 * Libraries required (arduino-cli lib install):
 *   - "FastLED"
 *   - "RF24"
 */

#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <FastLED.h>

// ============================================================
//  Application identity
// ============================================================
#define APP_NAME    "Orchestra-Node"
#define APP_VERSION "1.0.0"

// ============================================================
//  Pin assignments
//  RF-Nano has NRF24L01 wired internally to:
//    CE=D10, CSN=D9, SPI=D11/D12/D13
//  Remaining pins available for application:
// ============================================================
#define PIN_LED_DATA    2   // WS2812B data line

#define PIN_TEST_MODE   3   // Jumper LOW  → test mode active

#define PIN_ADDR_BIT0   4   // Address jumpers (active LOW, pull-up)
#define PIN_ADDR_BIT1   5
#define PIN_ADDR_BIT2   6
#define PIN_ADDR_BIT3   7
#define PIN_ADDR_BIT4   8
#define PIN_ADDR_BIT5   A0  // bit5 (MSB) — gives range 0-63

// RF-Nano built-in radio pins
#define RF_CE_PIN       10
#define RF_CSN_PIN      9

// ============================================================
//  LED configuration
// ============================================================
#define NUM_LEDS        10
#define LED_TYPE        WS2812B
#define COLOR_ORDER     GRB

// ============================================================
//  DMX / protocol constants
// ============================================================
#define MAGIC_STR           "START_LEDS"
#define MAGIC_LEN           10          // strlen("START_LEDS")
#define CHANNELS_PER_LED    3           // R, G, B
#define CHANNELS_PER_NODE   (NUM_LEDS * CHANNELS_PER_LED)  // 30
#define NODES_PER_UNIVERSE  (512 / CHANNELS_PER_NODE)      // 17
#define PACKET_OVERHEAD     (MAGIC_LEN + 2 + 1 + 4)        // magic+len+univ+crc = 17
#define MAX_PACKET_SIZE     (PACKET_OVERHEAD + 512)         // 529

// ============================================================
//  Radio
// ============================================================
// All nodes share the same listening pipe → gateway broadcasts
static const uint64_t PIPE_ADDR = 0xE8E8F0F0E1LL;

// NRF24L01 sends fixed 32-byte payloads; gateway fragments accordingly
#define RADIO_PAYLOAD_SIZE  32

// ============================================================
//  Timing
// ============================================================
#define RX_TIMEOUT_MS   2000UL   // blank LEDs if no valid packet
#define TEST_CHASE_MS    100UL   // chase step interval in test mode

// ============================================================
//  Globals
// ============================================================
RF24  radio(RF_CE_PIN, RF_CSN_PIN);
CRGB  leds[NUM_LEDS];

uint8_t  my_address  = 0;
bool     test_mode   = false;
bool     leds_on     = false;
unsigned long last_valid_rx = 0;

// ============================================================
//  CRC-32 (nibble-at-a-time, saves flash vs full table)
// ============================================================
static const uint32_t CRC_TABLE[16] PROGMEM = {
    0x00000000UL, 0x1db71064UL, 0x3b6e20c8UL, 0x26d930acUL,
    0x76dc4190UL, 0x6b6b51f4UL, 0x4db26158UL, 0x5005713cUL,
    0xedb88320UL, 0xf00f9344UL, 0xd6d6a3e8UL, 0xcb61b38cUL,
    0x9b64c2b0UL, 0x86d3d2d4UL, 0xa00ae278UL, 0xbdbdf21cUL,
};

static inline uint32_t crc32_byte(uint32_t crc, uint8_t b) {
    crc ^= b;
    crc = pgm_read_dword(&CRC_TABLE[crc & 0x0f]) ^ (crc >> 4);
    crc = pgm_read_dword(&CRC_TABLE[crc & 0x0f]) ^ (crc >> 4);
    return crc;
}

uint32_t crc32_buf(const uint8_t *data, uint16_t len) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint16_t i = 0; i < len; i++) crc = crc32_byte(crc, data[i]);
    return crc ^ 0xFFFFFFFFUL;
}

// ============================================================
//  Packet parser — byte-by-byte state machine
// ============================================================
enum ParserState : uint8_t {
    PS_MAGIC = 0,
    PS_LEN_LOW,
    PS_LEN_HIGH,
    PS_UNIVERSE,
    PS_DMX_DATA,
    PS_CRC,
};

static ParserState  ps_state     = PS_MAGIC;
static uint8_t      ps_magic_idx = 0;
static uint16_t     ps_pkt_len   = 0;
static uint8_t      ps_universe  = 0;
static uint8_t      ps_dmx[512];
static uint16_t     ps_dmx_len   = 0;   // expected DMX bytes
static uint16_t     ps_dmx_rx   = 0;   // received so far
static uint8_t      ps_crc_buf[4];
static uint8_t      ps_crc_rx    = 0;

// Running CRC accumulator updated as bytes arrive (over magic+len+univ+dmx)
static uint32_t     ps_crc_acc   = 0xFFFFFFFFUL;

static void parser_reset() {
    ps_state     = PS_MAGIC;
    ps_magic_idx = 0;
    ps_crc_acc   = 0xFFFFFFFFUL;
}

// ============================================================
//  Address / jumper helpers
// ============================================================
uint8_t read_address() {
    uint8_t addr = 0;
    if (!digitalRead(PIN_ADDR_BIT0)) addr |= (1 << 0);
    if (!digitalRead(PIN_ADDR_BIT1)) addr |= (1 << 1);
    if (!digitalRead(PIN_ADDR_BIT2)) addr |= (1 << 2);
    if (!digitalRead(PIN_ADDR_BIT3)) addr |= (1 << 3);
    if (!digitalRead(PIN_ADDR_BIT4)) addr |= (1 << 4);
    if (!digitalRead(PIN_ADDR_BIT5)) addr |= (1 << 5);
    return addr;
}

bool read_test_mode() {
    return !digitalRead(PIN_TEST_MODE);
}

// ============================================================
//  LED helpers
// ============================================================
void leds_blank() {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    leds_on = false;
}

void apply_dmx(const uint8_t *dmx_data, uint16_t offset) {
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        uint16_t ch = offset + (uint16_t)i * CHANNELS_PER_LED;
        leds[i].r = dmx_data[ch];
        leds[i].g = dmx_data[ch + 1];
        leds[i].b = dmx_data[ch + 2];
    }
    FastLED.show();
    leds_on = true;
}

void do_test_chase() {
    static uint8_t  pos      = 0;
    static unsigned long ts  = 0;
    if (millis() - ts < TEST_CHASE_MS) return;
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    leds[pos] = CRGB(0, 0, 255);
    FastLED.show();
    pos = (pos + 1) % NUM_LEDS;
    ts  = millis();
}

// ============================================================
//  Packet validation and DMX application
// ============================================================
void process_packet() {
    // Finalise running CRC (covers magic + len + universe + dmx)
    uint32_t computed_crc = ps_crc_acc ^ 0xFFFFFFFFUL;

    // Reconstruct received CRC (little-endian)
    uint32_t received_crc = (uint32_t)ps_crc_buf[0]
                          | ((uint32_t)ps_crc_buf[1] << 8)
                          | ((uint32_t)ps_crc_buf[2] << 16)
                          | ((uint32_t)ps_crc_buf[3] << 24);

    if (computed_crc != received_crc) {
        Serial.print(F("[WARN] CRC fail  got=0x"));
        Serial.print(received_crc, HEX);
        Serial.print(F("  exp=0x"));
        Serial.println(computed_crc, HEX);
        return;
    }

    last_valid_rx = millis();

    if (my_address == 0) {
        Serial.println(F("[WARN] Address=0, ignoring data"));
        return;
    }

    // Determine which universe our node belongs to
    // Universe U holds nodes [ U*NPU+1 .. (U+1)*NPU ]
    uint8_t my_universe = (uint8_t)((my_address - 1) / NODES_PER_UNIVERSE);

    if (ps_universe != my_universe) return;  // not our universe

    // Compute byte offset within DMX data for this node
    uint8_t  first_node = my_universe * NODES_PER_UNIVERSE + 1;
    uint16_t offset     = (uint16_t)(my_address - first_node) * CHANNELS_PER_NODE;

    if (offset + CHANNELS_PER_NODE > ps_dmx_len) {
        Serial.println(F("[WARN] Node data out of DMX payload bounds"));
        return;
    }

    Serial.print(F("[INFO] DMX -> LEDs  univ="));
    Serial.print(ps_universe);
    Serial.print(F("  off="));
    Serial.print(offset);
    Serial.print(F("  addr="));
    Serial.println(my_address);

    apply_dmx(ps_dmx, offset);
}

// ============================================================
//  Feed one byte through the parser
// ============================================================
void feed_byte(uint8_t b) {
    switch (ps_state) {

    case PS_MAGIC:
        if (b == (uint8_t)MAGIC_STR[ps_magic_idx]) {
            // Accumulate CRC as magic bytes arrive
            ps_crc_acc = crc32_byte(ps_crc_acc, b);
            ps_magic_idx++;
            if (ps_magic_idx == MAGIC_LEN) {
                ps_state = PS_LEN_LOW;
            }
        } else {
            // Mismatch — restart, check if this byte opens a new magic sequence
            ps_crc_acc   = 0xFFFFFFFFUL;
            ps_magic_idx = 0;
            if (b == (uint8_t)MAGIC_STR[0]) {
                ps_crc_acc = crc32_byte(ps_crc_acc, b);
                ps_magic_idx = 1;
            }
        }
        break;

    case PS_LEN_LOW:
        ps_pkt_len = b;
        ps_crc_acc = crc32_byte(ps_crc_acc, b);
        ps_state   = PS_LEN_HIGH;
        break;

    case PS_LEN_HIGH:
        ps_pkt_len |= ((uint16_t)b << 8);
        ps_crc_acc  = crc32_byte(ps_crc_acc, b);
        // Sanity check
        if (ps_pkt_len < PACKET_OVERHEAD || ps_pkt_len > MAX_PACKET_SIZE) {
            Serial.print(F("[WARN] Bad pkt_len="));
            Serial.println(ps_pkt_len);
            parser_reset();
        } else {
            ps_dmx_len = ps_pkt_len - PACKET_OVERHEAD;
            ps_dmx_rx  = 0;
            ps_state   = PS_UNIVERSE;
        }
        break;

    case PS_UNIVERSE:
        ps_universe = b;
        ps_crc_acc  = crc32_byte(ps_crc_acc, b);
        if (ps_dmx_len == 0) {
            ps_crc_rx = 0;
            ps_state  = PS_CRC;
        } else {
            ps_state = PS_DMX_DATA;
        }
        break;

    case PS_DMX_DATA:
        ps_dmx[ps_dmx_rx] = b;
        ps_crc_acc = crc32_byte(ps_crc_acc, b);
        ps_dmx_rx++;
        if (ps_dmx_rx >= ps_dmx_len) {
            ps_crc_rx = 0;
            ps_state  = PS_CRC;
        }
        break;

    case PS_CRC:
        ps_crc_buf[ps_crc_rx++] = b;
        if (ps_crc_rx == 4) {
            process_packet();
            parser_reset();
        }
        break;
    }
}

// ============================================================
//  Radio init
// ============================================================
bool radio_init() {
    if (!radio.begin()) return false;
    radio.setPALevel(RF24_PA_LOW);
    radio.setDataRate(RF24_250KBPS);
    radio.setChannel(76);
    radio.setAutoAck(false);          // broadcast — no ACK expected
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

    // Jumper pins — active LOW with internal pull-up
    uint8_t input_pins[] = {
        PIN_TEST_MODE,
        PIN_ADDR_BIT0, PIN_ADDR_BIT1, PIN_ADDR_BIT2,
        PIN_ADDR_BIT3, PIN_ADDR_BIT4, PIN_ADDR_BIT5,
    };
    for (uint8_t i = 0; i < sizeof(input_pins); i++) {
        pinMode(input_pins[i], INPUT_PULLUP);
    }

    my_address = read_address();
    test_mode  = read_test_mode();

    Serial.print(F("Node address : "));
    Serial.println(my_address);
    Serial.print(F("Test mode    : "));
    Serial.println(test_mode ? F("ON") : F("OFF"));

    // LEDs
    FastLED.addLeds<LED_TYPE, PIN_LED_DATA, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.setBrightness(200);
    leds_blank();

    // Radio
    if (!radio_init()) {
        Serial.println(F("[ERROR] Radio init failed — halting"));
        // Signal error on LEDs: red blink loop
        while (true) {
            fill_solid(leds, NUM_LEDS, CRGB::Red);
            FastLED.show();
            delay(300);
            leds_blank();
            delay(300);
        }
    }

    Serial.println(F("Radio OK — listening"));
    last_valid_rx = millis();
}

// ============================================================
//  Main loop
// ============================================================
void loop() {
    // --- Re-read jumpers every iteration (immediate effect) ---
    uint8_t new_addr = read_address();
    bool    new_test = read_test_mode();

    if (new_addr != my_address) {
        my_address = new_addr;
        Serial.print(F("[INFO] Address changed -> "));
        Serial.println(my_address);
        leds_blank();
        parser_reset();
    }

    if (new_test != test_mode) {
        test_mode = new_test;
        Serial.print(F("[INFO] Test mode -> "));
        Serial.println(test_mode ? F("ON") : F("OFF"));
        if (!test_mode) leds_blank();
    }

    // --- Test mode: chenillard, ignore radio ---
    if (test_mode) {
        do_test_chase();
        return;
    }

    // --- Drain radio FIFO ---
    uint8_t pipe;
    while (radio.available(&pipe)) {
        uint8_t payload[RADIO_PAYLOAD_SIZE];
        radio.read(payload, RADIO_PAYLOAD_SIZE);
        for (uint8_t i = 0; i < RADIO_PAYLOAD_SIZE; i++) {
            feed_byte(payload[i]);
        }
    }

    // --- RX timeout: blank LEDs if no valid packet for 2 s ---
    if (leds_on && (millis() - last_valid_rx > RX_TIMEOUT_MS)) {
        Serial.println(F("[INFO] RX timeout — blanking LEDs"));
        leds_blank();
    }
}
