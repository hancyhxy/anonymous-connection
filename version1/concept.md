# Physical Computing — Final Project Concept

Course: Physical Computing Social Path
Last Updated: 2026-02-24

---

## Project Overview

An experimental music launchpad that connects performers, music, and listeners.

The core idea: performers' **biometric data** (heart rate, hand pressure, movement speed, distance) is captured in real-time and translated into **visual and audio changes** — making the performer's invisible emotional state perceptible to the audience.

**Key difference from traditional performance:**
Traditional music performance has a natural gap — the performer's internal experience (racing heart, muscle tension, emotional surge) is invisible to the audience. This launchpad bridges that gap.

---

## Stage 1 — Concept

### (a) Individual Concept Development

**Three possible angles to define the concept:**

**Angle 1: Body as Instrument**
The performer's biological signals are not just supplementary information — they directly participate in music generation. Heart rate affects tempo, pressure affects volume, movement affects effects.
→ Emphasis: performer's physical state = part of the performance

**Angle 2: Emotion Made Visible**
Biometric data is translated into visuals or sound, allowing the audience to "see" the performer's emotional state in real-time.
→ Emphasis: authentic emotional connection, not just music

**Angle 3: Performer–Music–Audience Loop**
Performer, music system, and audience form a real-time feedback loop. The audience's reaction could even feed back into the performance.
→ Emphasis: the entire performance is a dynamic, living system

**Current status:** Deciding on a primary angle. These three can combine, but one main axis is needed for the concept stage.

---

### (b) Research

*To be filled in — topics to explore:*
- Existing biometric music projects / precedents
- Sensor types and feasibility (heart rate, pressure, motion)
- Real-time data-to-audio/visual mapping techniques
- Audience experience research in experimental performance

---

## Stage 2 — Final Plan & User Research

### (a) Choose Final Mixed Concept

*To be filled in after group discussion*

### (b) User Research

*To be filled in — key questions:*
- What biometric signals are most meaningful to audiences?
- How should data be translated (subtle vs. dramatic changes)?
- What visual/audio responses create the strongest emotional resonance?

---

## Stage 3 — Prototype & Showcase

### (a) Build Prototype

*To be filled in*

### (b) Demo & Showcase

*To be filled in*

---

## Notes & Open Questions

- Which biometric data sources to prioritize?
- Individual vs. group performance context?
- How much of the system should be visible/transparent to the audience?

---

## Why this version was superseded (added 2026-04-21)

Version 1 stopped at the prototype-building phase. The hardware path (ESP32 DEVKIT V1 + a bare ST7789 1.3" display) required hand-wiring multiple data/control lines, and the wiring complexity became the main blocker — see the ChatGPT thread titled "Arduino OLED 屏幕接法" from that period.

Two decisions came out of that blocker:

1. **Hardware**: move to `ESP32-C6-LCD-1.3-M` — a board with the LCD integrated and header pins pre-soldered, removing the wiring step entirely.
2. **Concept**: pivot away from the biometric-music-launchpad direction toward an anonymous avatar + interest-tag matching device (see `../version2/concept.md`).

Everything in this file is preserved as-is; the driver libraries and research notes in this directory remain as v1 artifacts.
