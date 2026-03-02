# Orchestra Wireless LED Control

A distributed wireless LED control system for live performances,
enabling real-time DMX (QLC+) control of individually addressable
wearable LED nodes over radio.

## Overview

This project provides a scalable architecture to control wearable LED
strips used by up to 50 musicians during live performances.

Each musician wears a small LED band (up to 10 WS2812B LEDs) driven by
an Arduino Nano with a dedicated radio receiver.\
A central gateway receives DMX data from QLC+ (via Art-Net or sACN) and
broadcasts synchronized lighting frames to all wireless nodes.

The system is designed for:

-   Low latency
-   Deterministic synchronization
-   Scalable multi-node control
-   Real-time stage performance reliability

------------------------------------------------------------------------

## System Architecture

QLC+ (DMX / Art-Net / sACN) ↓ Ethernet Network ↓ Gateway (Raspberry Pi)
↓ Radio Broadcast ↓ 50x Arduino Nano Nodes ↓ WS2812B LED Bands

------------------------------------------------------------------------

## Features

-   Up to 50 independent wireless LED nodes
-   10 RGB LEDs per node (individually addressable)
-   Real-time DMX universe mapping
-   Multi-universe support (Art-Net / sACN)
-   Frame synchronization via sequence numbering
-   Compact RF protocol for efficient bandwidth usage
-   Designed for live performance environments

------------------------------------------------------------------------

## DMX Mapping

Each musician is assigned a fixed DMX address block:

-   10 LEDs × 3 channels (RGB) = 30 DMX channels per node
-   50 nodes = 1500 DMX channels
-   Requires 3 DMX universes (512 channels each)

Mapping strategy is configurable in the gateway.

------------------------------------------------------------------------

## Hardware Components

### Gateway

-   Raspberry Pi
-   Radio transmitter module (e.g., nRF24L01+)

### Node (per musician)

-   Arduino Nano (ATmega328P)
-   Radio receiver module
-   WS2812B LED strip (max 10 LEDs)
-   5V power supply

------------------------------------------------------------------------

## Design Goals

-   Minimal RF latency
-   High reliability in crowded RF environments
-   Deterministic frame updates
-   Simple addressing model per musician
-   Modular firmware and gateway architecture

------------------------------------------------------------------------

## Repository Structure

/gateway → DMX receiver + RF broadcaster\
/firmware → Arduino Nano LED node firmware\
/docs → Protocol specification and architecture\
/hardware → Schematics and wiring diagrams

------------------------------------------------------------------------

## Status

Project in active development.

------------------------------------------------------------------------

## License

[GPLv2](LICENSE)
