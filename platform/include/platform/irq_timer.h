/*
 * platform/include/platform/irq_timer.h
 *
 * Interrupts that arrive while game code is busy.
 *
 * On the GBA, V-blank interrupts preempt whatever the CPU is doing, and some
 * game code relies on that: it busy-waits for work done in the V-blank
 * handler (e.g. the map loader waits for the DMA3 manager to copy tilesets).
 * The host normally raises the frame's interrupts after the game's frame
 * returns, so such a wait would never end. While armed, this timer raises
 * them on schedule instead -- every GBA frame time -- if the game code is
 * still running, just like a lag frame on hardware.
 */

#ifndef PLATFORM_IRQ_TIMER_H
#define PLATFORM_IRQ_TIMER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Arm: call `raise` every GBA frame time until disarmed. `raise` runs
 * asynchronously (from a signal handler), preempting game code exactly
 * where the hardware would; it must only run game interrupt handlers. */
void IrqTimer_Arm(void (*raise)(void));
void IrqTimer_Disarm(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_IRQ_TIMER_H */
