/*
 * platform/src/host_rfu.c
 *
 * Stand-in for the GBA wireless adapter library (reference/src/librfu_*.c and
 * AgbRfu_LinkManager.c, excluded from the host build).
 *
 * Behaves as if no wireless adapter is plugged in: the API initialises
 * successfully (as it does on hardware without an adapter), but the adapter
 * ID check fails, so link.c's IsWirelessAdapterConnected() reports FALSE and
 * the game never starts a wireless session. The status structures are real,
 * zeroed storage so code that inspects them reads "no partners". Phase 16
 * replaces this with a network backend.
 */

/* libc first: global.h defines function-like macros that clash with it. */
#include <string.h>

#include "global.h"
#include "librfu.h"
#include "link_rfu.h"
#include "AgbRfu_LinkManager.h"

struct linkManagerTag lman;

static struct RfuLinkStatus sLinkStatus;
static struct RfuSlotStatusNI sSlotStatusNI[RFU_CHILD_MAX];
static struct RfuSlotStatusUNI sSlotStatusUNI[RFU_CHILD_MAX];

struct RfuLinkStatus *gRfuLinkStatus = &sLinkStatus;
struct RfuSlotStatusNI *gRfuSlotStatusNI[RFU_CHILD_MAX] = {
    &sSlotStatusNI[0], &sSlotStatusNI[1], &sSlotStatusNI[2], &sSlotStatusNI[3],
};
struct RfuSlotStatusUNI *gRfuSlotStatusUNI[RFU_CHILD_MAX] = {
    &sSlotStatusUNI[0], &sSlotStatusUNI[1], &sSlotStatusUNI[2], &sSlotStatusUNI[3],
};

/* librfu_rfu.c */

u16 rfu_initializeAPI(u32 *APIBuffer, u16 buffByteSize, IntrFunc *sioIntrTable_p, bool8 copyInterruptToRam)
{
    (void)APIBuffer;
    (void)buffByteSize;
    (void)sioIntrTable_p;
    (void)copyInterruptToRam;
    memset(&sLinkStatus, 0, sizeof(sLinkStatus));
    memset(sSlotStatusNI, 0, sizeof(sSlotStatusNI));
    memset(sSlotStatusUNI, 0, sizeof(sSlotStatusUNI));
    return 0;
}

void rfu_setTimerInterrupt(u8 timerNo, IntrFunc *timerIntrTable_p) { (void)timerNo; (void)timerIntrTable_p; }
u16 rfu_waitREQComplete(void) { return 0; }
void rfu_REQ_stopMode(void) { }
void rfu_REQ_disconnect(u8 bmDisconnectSlot) { (void)bmDisconnectSlot; }
void rfu_REQ_recvData(void) { }
void rfu_REQ_PARENT_resumeRetransmitAndChange(void) { }

void rfu_REQ_configGameData(u8 mbootFlag, u16 serialNo, const u8 *gname, const u8 *uname)
{
    (void)mbootFlag;
    (void)serialNo;
    (void)gname;
    (void)uname;
}

u16 rfu_clearSlot(u8 connTypeFlag, u8 slotStatusIndex) { (void)connTypeFlag; (void)slotStatusIndex; return 0; }
void rfu_clearAllSlot(void) { }

u16 rfu_setRecvBuffer(u8 connType, u8 slotNo, void *buffer, u32 buffSize)
{
    (void)connType;
    (void)slotNo;
    (void)buffer;
    (void)buffSize;
    return 0;
}

u16 rfu_NI_setSendData(u8 bmSendSlot, u8 subFrameSize, const void *src, u32 size)
{
    (void)bmSendSlot;
    (void)subFrameSize;
    (void)src;
    (void)size;
    return 0;
}

u16 rfu_UNI_setSendData(u8 bmSendSlot, const void *src, u8 size)
{
    (void)bmSendSlot;
    (void)src;
    (void)size;
    return 0;
}

void rfu_UNI_readySendData(u8 slotStatusIndex) { (void)slotStatusIndex; }
void rfu_UNI_clearRecvNewDataFlag(u8 slotStatusIndex) { (void)slotStatusIndex; }

/* AgbRfu_LinkManager.c */

u8 rfu_LMAN_initializeManager(void (*LMAN_callback_p)(u8, u8), void (*MSC_callback_p)(u16))
{
    (void)LMAN_callback_p;
    (void)MSC_callback_p;
    memset(&lman, 0, sizeof(lman));
    return 0;
}

void rfu_LMAN_initializeRFU(INIT_PARAM *init_parameters) { (void)init_parameters; }

/* Anything but RFU_ID means "no adapter" to IsWirelessAdapterConnected. */
u32 rfu_LMAN_REQBN_softReset_and_checkID(void) { return 0; }

void rfu_LMAN_powerDownRFU(void) { }
void rfu_LMAN_stopManager(u8 forced_stop_and_RFU_reset_flag) { (void)forced_stop_and_RFU_reset_flag; }
void rfu_LMAN_manager_entity(u32 rand) { (void)rand; }
void rfu_LMAN_syncVBlank(void) { }
void rfu_LMAN_forceChangeSP(void) { }
void rfu_LMAN_requestChangeAgbClockMaster(void) { }
void rfu_LMAN_REQ_sendData(bool8 clockChangeFlag) { (void)clockChangeFlag; }
void rfu_LMAN_setMSCCallback(void (*MSC_callback_p)(u16)) { (void)MSC_callback_p; }

u8 rfu_LMAN_establishConnection(u8 parent_child, u16 connect_period, u16 name_accept_period, u16 *acceptable_serialNo_list)
{
    (void)parent_child;
    (void)connect_period;
    (void)name_accept_period;
    (void)acceptable_serialNo_list;
    return 0;
}

u8 rfu_LMAN_CHILD_connectParent(u16 parentId, u16 connect_period)
{
    (void)parentId;
    (void)connect_period;
    return 0;
}

u8 rfu_LMAN_setLinkRecovery(u8 enable_flag, u16 recovery_period)
{
    (void)enable_flag;
    (void)recovery_period;
    return 0;
}
