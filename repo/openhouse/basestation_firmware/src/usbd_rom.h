/* NXP LPC11Uxx boot-ROM USB device API (subset).
 * Layouts follow NXP LPCOpen usbd_rom_api.h / usbd_hw.h / usbd_core.h / usbd_cdc.h;
 * they describe tables inside the mask ROM, so field order must not change. */
#pragma once
#include <stdint.h>

typedef void *USBD_HANDLE_T;
typedef int32_t ErrorCode_t;
#define LPC_OK 0

typedef ErrorCode_t (*USB_CB_T)(USBD_HANDLE_T hUsb);
typedef ErrorCode_t (*USB_PARAM_CB_T)(USBD_HANDLE_T hUsb, uint32_t param1);
typedef ErrorCode_t (*USB_EP_HANDLER_T)(USBD_HANDLE_T hUsb, void *data, uint32_t event);

/* endpoint events */
#define USB_EVT_SETUP       1
#define USB_EVT_OUT         2
#define USB_EVT_IN          3
#define USB_EVT_OUT_NAK     4
#define USB_EVT_IN_NAK      5

typedef struct {
    uint32_t usb_reg_base;
    uint32_t mem_base;
    uint32_t mem_size;
    uint8_t  max_num_ep;
    uint8_t  pad0[3];
    USB_CB_T USB_Reset_Event;
    USB_CB_T USB_Suspend_Event;
    USB_CB_T USB_Resume_Event;
    USB_CB_T reserved_sbz;
    USB_CB_T USB_SOF_Event;
    USB_PARAM_CB_T USB_WakeUpCfg;
    USB_PARAM_CB_T USB_Power_Event;
    USB_PARAM_CB_T USB_Error_Event;
    USB_CB_T USB_Configure_Event;
    USB_CB_T USB_Interface_Event;
    USB_CB_T USB_Feature_Event;
    uint32_t (*virt_to_phys)(void *vaddr);
    void (*cache_flush)(uint32_t *start_adr, uint32_t *end_adr);
} USBD_API_INIT_PARAM_T;

typedef struct {
    uint8_t *device_desc;
    uint8_t *string_desc;
    uint8_t *full_speed_desc;
    uint8_t *high_speed_desc;
    uint8_t *device_qualifier;
} USB_CORE_DESCS_T;

/* Hardware API table. Order VERIFIED against this chip's boot ROM (dump in
 * re/rom/lpc11u37_bootrom.bin, table @0x1FFF1F80, 23 entries): it differs from
 * the LPCOpen header (EnableEvent is last; ReadReqEP is a stub returning 0 -
 * the ROM re-arms OUT endpoints itself). Entries we don't call keep neutral names. */
typedef struct {
    uint32_t (*GetMemSize)(USBD_API_INIT_PARAM_T *param);                    /*  0 */
    ErrorCode_t (*Init)(USBD_HANDLE_T *phUsb, USB_CORE_DESCS_T *pDesc,
                        USBD_API_INIT_PARAM_T *param);                       /*  1 */
    void (*Connect)(USBD_HANDLE_T hUsb, uint32_t con);                       /*  2 */
    void (*ISR)(USBD_HANDLE_T hUsb);                                         /*  3 */
    void (*Reset)(USBD_HANDLE_T hUsb);                                       /*  4 */
    void *fn5, *fn6, *fn7;                                                   /*  5-7 (speed/wakeup/power) */
    void (*Configure)(USBD_HANDLE_T hUsb, uint32_t cfg);                     /*  8 */
    void (*ConfigEP)(USBD_HANDLE_T hUsb, void *pEPD);                        /*  9 */
    void *fn10;                                                              /* 10 (stub) */
    void (*EnableEP)(USBD_HANDLE_T hUsb, uint32_t EPNum);                    /* 11 */
    void (*DisableEP)(USBD_HANDLE_T hUsb, uint32_t EPNum);                   /* 12 */
    void (*ResetEP)(USBD_HANDLE_T hUsb, uint32_t EPNum);                     /* 13 */
    void (*SetStallEP)(USBD_HANDLE_T hUsb, uint32_t EPNum);                  /* 14 */
    void (*ClrStallEP)(USBD_HANDLE_T hUsb, uint32_t EPNum);                  /* 15 */
    ErrorCode_t (*SetTestMode)(USBD_HANDLE_T hUsb, uint8_t mode);            /* 16 */
    uint32_t (*ReadEP)(USBD_HANDLE_T hUsb, uint32_t EPNum, uint8_t *pData);  /* 17 */
    uint32_t (*ReadReqEP)(USBD_HANDLE_T hUsb, uint32_t EPNum,
                          uint8_t *pData, uint32_t len);                     /* 18 stub */
    uint32_t (*ReadSetupPkt)(USBD_HANDLE_T hUsb, uint32_t EPNum, uint32_t *pData); /* 19 */
    uint32_t (*WriteEP)(USBD_HANDLE_T hUsb, uint32_t EPNum,
                        uint8_t *pData, uint32_t cnt);                       /* 20 */
    void (*WakeUp)(USBD_HANDLE_T hUsb);                                      /* 21 */
    ErrorCode_t (*EnableEvent)(USBD_HANDLE_T hUsb, uint32_t EPNum,
                               uint32_t event_type, uint32_t enable);        /* 22 */
} USBD_HW_API_T;

typedef struct {
    ErrorCode_t (*RegisterClassHandler)(USBD_HANDLE_T hUsb, USB_EP_HANDLER_T pfn, void *data);
    ErrorCode_t (*RegisterEpHandler)(USBD_HANDLE_T hUsb, uint32_t ep_index, USB_EP_HANDLER_T pfn, void *data);
    void (*SetupStage)(USBD_HANDLE_T hUsb);
    void (*DataInStage)(USBD_HANDLE_T hUsb);
    void (*DataOutStage)(USBD_HANDLE_T hUsb);
    void (*StatusInStage)(USBD_HANDLE_T hUsb);
    void (*StatusOutStage)(USBD_HANDLE_T hUsb);
    void (*StallEp0)(USBD_HANDLE_T hUsb);
} USBD_CORE_API_T;

typedef struct {
    uint32_t mem_base;
    uint32_t mem_size;
    uint8_t *cif_intf_desc;
    uint8_t *dif_intf_desc;
    ErrorCode_t (*CIC_GetRequest)(USBD_HANDLE_T, void *pSetup, uint8_t **pBuffer, uint16_t *length);
    ErrorCode_t (*CIC_SetRequest)(USBD_HANDLE_T, void *pSetup, uint8_t **pBuffer, uint16_t length);
    ErrorCode_t (*CDC_BulkIN_Hdlr)(USBD_HANDLE_T, void *data, uint32_t event);
    ErrorCode_t (*CDC_BulkOUT_Hdlr)(USBD_HANDLE_T, void *data, uint32_t event);
    ErrorCode_t (*SendEncpsCmd)(USBD_HANDLE_T, uint8_t *buffer, uint16_t len);
    ErrorCode_t (*GetEncpsResp)(USBD_HANDLE_T, uint8_t **buffer, uint16_t *len);
    ErrorCode_t (*SetCommFeature)(USBD_HANDLE_T, uint16_t feature, uint8_t *buffer, uint16_t len);
    ErrorCode_t (*GetCommFeature)(USBD_HANDLE_T, uint16_t feature, uint8_t **pBuffer, uint16_t *len);
    ErrorCode_t (*ClrCommFeature)(USBD_HANDLE_T, uint16_t feature);
    ErrorCode_t (*SetCtrlLineState)(USBD_HANDLE_T, uint16_t state);
    ErrorCode_t (*SendBreak)(USBD_HANDLE_T, uint16_t mstime);
    ErrorCode_t (*SetLineCode)(USBD_HANDLE_T, void *line_coding);
    ErrorCode_t (*CDC_InterruptEP_Hdlr)(USBD_HANDLE_T, void *data, uint32_t event);
    ErrorCode_t (*CDC_Ep0_Hdlr)(USBD_HANDLE_T, void *data, uint32_t event);
} USBD_CDC_INIT_PARAM_T;

typedef struct {
    uint32_t (*GetMemSize)(USBD_CDC_INIT_PARAM_T *param);
    ErrorCode_t (*init)(USBD_HANDLE_T hUsb, USBD_CDC_INIT_PARAM_T *param, USBD_HANDLE_T *phCDC);
    ErrorCode_t (*SendNotification)(USBD_HANDLE_T hCdc, uint8_t bNotification, uint16_t data);
} USBD_CDC_API_T;

typedef struct {
    const USBD_HW_API_T   *hw;
    const USBD_CORE_API_T *core;
    const void            *msc;
    const void            *dfu;
    const void            *hid;
    const USBD_CDC_API_T  *cdc;
    const uint32_t        *reserved6;
    uint32_t               version;
} USBD_API_T;

typedef struct {
    const USBD_API_T *pUSBD;
} ROM_API_T;

#define ROM_API         (*(ROM_API_T * const *)0x1FFF1FF8)
#define USBD_API        (ROM_API->pUSBD)

/* ROM endpoint-handler index from an endpoint address */
#define USB_EP_INDEX_IN(n)   ((((n) & 0x0F) << 1) + 1)
#define USB_EP_INDEX_OUT(n)  (((n) & 0x0F) << 1)
