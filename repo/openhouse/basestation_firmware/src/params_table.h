/* Generated once from the stock firmware's parameter table @0xECF0.
 * Do not edit: order, sizes and flags define the EEPROM payload layout
 * (stock cparam.c, 551 stored bytes). */
#pragma once

#define ENUM_E5CC_COUNT 5
#define ENUM_E5E4_COUNT 3
#define ENUM_11E7C_COUNT 5
#define ENUM_E580_COUNT 3

/* P(field, name, ptype, ctype, size, flags, default, enum, description: empty, the stock text is not copied)
 * flags bit0 = runtime only (not stored), bit1 = factory calibration */
#define PARAM_LIST(P) \
    P(sys_param_magic             , "sys.param_magic", PT_U32     , uint32_t,   4, 0x00, 1634492787, NULL, "") \
    P(sys_name                    , "sys.name", PT_STRING  , char    ,  16, 0x00, "DEFAULT", NULL, "") \
    P(timebase_debug              , "timebase.debug", PT_BOOL    , bool    ,   1, 0x00, false, NULL, "") \
    P(timebase_mode               , "timebase.mode", PT_ENUM    , uint8_t ,   1, 0x00, 3, enum_e5cc, "") \
    P(timebase_period             , "timebase.period", PT_U32     , uint32_t,   4, 0x00, 800000, NULL, "") \
    P(timebase_offset             , "timebase.offset", PT_I32     , int32_t ,   4, 0x00, 0, NULL, "") \
    P(timebase_optical_mask       , "timebase.optical.mask", PT_BOOL    , bool    ,   1, 0x00, true, NULL, "") \
    P(timebase_optical_presync    , "timebase.optical.presync", PT_U32     , uint32_t,   4, 0x00, 4000, NULL, "") \
    P(timebase_optical_offset     , "timebase.optical.offset", PT_U32     , uint32_t,   4, 0x00, 20000, NULL, "") \
    P(timebase_p                  , "timebase.p", PT_FLOAT   , float   ,   4, 0x00, 0.05f, NULL, "") \
    P(timebase_i                  , "timebase.i", PT_FLOAT   , float   ,   4, 0x00, 0.0f, NULL, "") \
    P(timebase_d                  , "timebase.d", PT_FLOAT   , float   ,   4, 0x00, 1.0f, NULL, "") \
    P(timebase_k                  , "timebase.k", PT_FLOAT   , float   ,   4, 0x00, 0.999f, NULL, "") \
    P(timebase_locked             , "timebase.locked", PT_BOOL    , bool    ,   1, 0x01, true, NULL, "") \
    P(timebase_source             , "timebase.source", PT_ENUM    , uint8_t ,   1, 0x01, 0, enum_e5e4, "") \
    P(timebase_current            , "timebase.current", PT_U32     , uint32_t,   4, 0x01, 0, NULL, "") \
    P(timebase_tdm                , "timebase.tdm", PT_BOOL    , bool    ,   1, 0x01, false, NULL, "") \
    P(corr_enable                 , "corr.enable", PT_BOOL    , bool    ,   1, 0x00, true, NULL, "") \
    P(corr_debug                  , "corr.debug", PT_BOOL    , bool    ,   1, 0x00, false, NULL, "") \
    P(corr_k                      , "corr.k", PT_FLOAT   , float   ,   4, 0x00, 0.999f, NULL, "") \
    P(corr_tolerance              , "corr.tolerance", PT_U32     , uint32_t,   4, 0x00, 400, NULL, "") \
    P(corr_settle                 , "corr.settle", PT_U32     , uint32_t,   4, 0x00, 300, NULL, "") \
    P(corr_timeout                , "corr.timeout", PT_U32     , uint32_t,   4, 0x00, 300, NULL, "") \
    P(corr_status                 , "corr.status", PT_ENUM    , uint8_t ,   1, 0x01, 0, enum_11e7c, "") \
    P(corr_period                 , "corr.period", PT_FLOAT   , float   ,   4, 0x01, 0.0f, NULL, "") \
    P(corr_missed                 , "corr.missed", PT_U32     , uint32_t,   4, 0x01, 0, NULL, "") \
    P(carrier_enable              , "carrier.enable", PT_BOOL    , bool    ,   1, 0x00, true, NULL, "") \
    P(carrier_debug               , "carrier.debug", PT_BOOL    , bool    ,   1, 0x00, false, NULL, "") \
    P(carrier_frequency           , "carrier.frequency", PT_U32     , uint32_t,   4, 0x00, 1843200, NULL, "") \
    P(laser_enable                , "laser.enable", PT_BOOL    , bool    ,   1, 0x00, true, NULL, "") \
    P(laser_debug                 , "laser.debug", PT_BOOL    , bool    ,   1, 0x00, false, NULL, "") \
    P(laser_interlock             , "laser.interlock", PT_BOOL    , bool    ,   1, 0x00, true, NULL, "") \
    P(laser_apc                   , "laser.apc", PT_BOOL    , bool    ,   1, 0x00, true, NULL, "") \
    P(laser_b1                    , "laser.b1", PT_FLOAT   , float   ,   4, 0x00, 0.95f, NULL, "") \
    P(laser_0_enable              , "laser.0.enable", PT_BOOL    , bool    ,   1, 0x00, true, NULL, "") \
    P(laser_0_fullspin            , "laser.0.fullspin", PT_BOOL    , bool    ,   1, 0x00, false, NULL, "") \
    P(laser_0_start               , "laser.0.start", PT_FLOAT   , float   ,   4, 0x00, 32.0f, NULL, "") \
    P(laser_0_length              , "laser.0.length", PT_FLOAT   , float   ,   4, 0x00, 124.0f, NULL, "") \
    P(laser_0_bias                , "laser.0.bias", PT_U8      , uint8_t ,   1, 0x00, 0, NULL, "") \
    P(laser_0_current             , "laser.0.current", PT_U8      , uint8_t ,   1, 0x00, 190, NULL, "") \
    P(laser_0_pwr                 , "laser.0.pwr", PT_FLOAT   , float   ,   4, 0x00, 30.0f, NULL, "") \
    P(laser_0_pwr_b               , "laser.0.pwr.b", PT_FLOAT   , float   ,   4, 0x00, 9.0f, NULL, "") \
    P(laser_0_pwr_m               , "laser.0.pwr.m", PT_FLOAT   , float   ,   4, 0x00, 0.434f, NULL, "") \
    P(laser_0_pwr_gain            , "laser.0.pwr.gain", PT_U16     , uint16_t,   2, 0x00, 3, NULL, "") \
    P(laser_0_pwr_detected        , "laser.0.pwr.detected", PT_FLOAT   , float   ,   4, 0x01, 0.0f, NULL, "") \
    P(laser_0_pwr_average         , "laser.0.pwr.average", PT_FLOAT   , float   ,   4, 0x01, 0.0f, NULL, "") \
    P(laser_1_enable              , "laser.1.enable", PT_BOOL    , bool    ,   1, 0x00, true, NULL, "") \
    P(laser_1_fullspin            , "laser.1.fullspin", PT_BOOL    , bool    ,   1, 0x00, false, NULL, "") \
    P(laser_1_start               , "laser.1.start", PT_FLOAT   , float   ,   4, 0x00, 25.0f, NULL, "") \
    P(laser_1_length              , "laser.1.length", PT_FLOAT   , float   ,   4, 0x00, 124.0f, NULL, "") \
    P(laser_1_bias                , "laser.1.bias", PT_U8      , uint8_t ,   1, 0x00, 0, NULL, "") \
    P(laser_1_current             , "laser.1.current", PT_U8      , uint8_t ,   1, 0x00, 190, NULL, "") \
    P(laser_1_pwr                 , "laser.1.pwr", PT_FLOAT   , float   ,   4, 0x00, 30.0f, NULL, "") \
    P(laser_1_pwr_b               , "laser.1.pwr.b", PT_FLOAT   , float   ,   4, 0x00, 9.0f, NULL, "") \
    P(laser_1_pwr_m               , "laser.1.pwr.m", PT_FLOAT   , float   ,   4, 0x00, 0.434f, NULL, "") \
    P(laser_1_pwr_gain            , "laser.1.pwr.gain", PT_U16     , uint16_t,   2, 0x00, 3, NULL, "") \
    P(laser_1_pwr_detected        , "laser.1.pwr.detected", PT_FLOAT   , float   ,   4, 0x01, 0.0f, NULL, "") \
    P(laser_1_pwr_average         , "laser.1.pwr.average", PT_FLOAT   , float   ,   4, 0x01, 0.0f, NULL, "") \
    P(rotor_0_enable              , "rotor.0.enable", PT_BOOL    , bool    ,   1, 0x00, true, NULL, "") \
    P(rotor_0_debug               , "rotor.0.debug", PT_BOOL    , bool    ,   1, 0x00, false, NULL, "") \
    P(rotor_0_pwm                 , "rotor.0.pwm", PT_U16     , uint16_t,   2, 0x00, 2000, NULL, "") \
    P(rotor_0_pwm_m               , "rotor.0.pwm.m", PT_FLOAT   , float   ,   4, 0x00, -0.026f, NULL, "") \
    P(rotor_0_pwm_b               , "rotor.0.pwm.b", PT_FLOAT   , float   ,   4, 0x00, 110.0f, NULL, "") \
    P(rotor_0_pll_debug           , "rotor.0.pll.debug", PT_BOOL    , bool    ,   1, 0x00, false, NULL, "") \
    P(rotor_0_pll_enable          , "rotor.0.pll.enable", PT_BOOL    , bool    ,   1, 0x00, true, NULL, "") \
    P(rotor_0_pll_p               , "rotor.0.pll.p", PT_FLOAT   , float   ,   4, 0x00, 0.02f, NULL, "") \
    P(rotor_0_pll_i               , "rotor.0.pll.i", PT_FLOAT   , float   ,   4, 0x00, 0.001f, NULL, "") \
    P(rotor_0_pll_d               , "rotor.0.pll.d", PT_FLOAT   , float   ,   4, 0x00, 0.2f, NULL, "") \
    P(rotor_0_pll_offset          , "rotor.0.pll.offset", PT_I32     , int32_t ,   4, 0x00, -200000, NULL, "") \
    P(rotor_0_tolerance           , "rotor.0.tolerance", PT_FLOAT   , float   ,   4, 0x00, 300.0f, NULL, "") \
    P(rotor_0_settle              , "rotor.0.settle", PT_U32     , uint32_t,   4, 0x00, 30, NULL, "") \
    P(rotor_0_autoclear           , "rotor.0.autoclear", PT_BOOL    , bool    ,   1, 0x00, true, NULL, "") \
    P(rotor_0_pwm_auto            , "rotor.0.pwm.auto", PT_BOOL    , bool    ,   1, 0x00, true, NULL, "") \
    P(rotor_0_pwm_i               , "rotor.0.pwm.i", PT_FLOAT   , float   ,   4, 0x00, 0.001f, NULL, "") \
    P(rotor_1_enable              , "rotor.1.enable", PT_BOOL    , bool    ,   1, 0x00, true, NULL, "") \
    P(rotor_1_debug               , "rotor.1.debug", PT_BOOL    , bool    ,   1, 0x00, false, NULL, "") \
    P(rotor_1_pwm                 , "rotor.1.pwm", PT_U16     , uint16_t,   2, 0x00, 2000, NULL, "") \
    P(rotor_1_pwm_m               , "rotor.1.pwm.m", PT_FLOAT   , float   ,   4, 0x00, -0.026f, NULL, "") \
    P(rotor_1_pwm_b               , "rotor.1.pwm.b", PT_FLOAT   , float   ,   4, 0x00, 110.0f, NULL, "") \
    P(rotor_1_pll_debug           , "rotor.1.pll.debug", PT_BOOL    , bool    ,   1, 0x00, false, NULL, "") \
    P(rotor_1_pll_enable          , "rotor.1.pll.enable", PT_BOOL    , bool    ,   1, 0x00, true, NULL, "") \
    P(rotor_1_pll_p               , "rotor.1.pll.p", PT_FLOAT   , float   ,   4, 0x00, 0.02f, NULL, "") \
    P(rotor_1_pll_i               , "rotor.1.pll.i", PT_FLOAT   , float   ,   4, 0x00, 0.001f, NULL, "") \
    P(rotor_1_pll_d               , "rotor.1.pll.d", PT_FLOAT   , float   ,   4, 0x00, 0.2f, NULL, "") \
    P(rotor_1_pll_offset          , "rotor.1.pll.offset", PT_I32     , int32_t ,   4, 0x00, 200000, NULL, "") \
    P(rotor_1_tolerance           , "rotor.1.tolerance", PT_FLOAT   , float   ,   4, 0x00, 300.0f, NULL, "") \
    P(rotor_1_settle              , "rotor.1.settle", PT_U32     , uint32_t,   4, 0x00, 30, NULL, "") \
    P(rotor_1_autoclear           , "rotor.1.autoclear", PT_BOOL    , bool    ,   1, 0x00, true, NULL, "") \
    P(rotor_1_pwm_auto            , "rotor.1.pwm.auto", PT_BOOL    , bool    ,   1, 0x00, true, NULL, "") \
    P(rotor_1_pwm_i               , "rotor.1.pwm.i", PT_FLOAT   , float   ,   4, 0x00, 0.001f, NULL, "") \
    P(stats_enable                , "stats.enable", PT_BOOL    , bool    ,   1, 0x00, false, NULL, "") \
    P(stats_interval              , "stats.interval", PT_U16     , uint16_t,   2, 0x00, 1, NULL, "") \
    P(ootx_enable                 , "ootx.enable", PT_BOOL    , bool    ,   1, 0x00, true, NULL, "") \
    P(ootx_debug                  , "ootx.debug", PT_BOOL    , bool    ,   1, 0x00, false, NULL, "") \
    P(ootx_lock_required          , "ootx.lock_required", PT_BOOL    , bool    ,   1, 0x00, true, NULL, "") \
    P(ootx_j0                     , "ootx.j0", PT_U16     , uint16_t,   2, 0x00, 3000, NULL, "") \
    P(ootx_j1                     , "ootx.j1", PT_U16     , uint16_t,   2, 0x00, 4000, NULL, "") \
    P(ootx_k0                     , "ootx.k0", PT_U16     , uint16_t,   2, 0x00, 3500, NULL, "") \
    P(ootx_k1                     , "ootx.k1", PT_U16     , uint16_t,   2, 0x00, 4500, NULL, "") \
    P(ootx_j2                     , "ootx.j2", PT_U16     , uint16_t,   2, 0x00, 5000, NULL, "") \
    P(ootx_j3                     , "ootx.j3", PT_U16     , uint16_t,   2, 0x00, 6000, NULL, "") \
    P(ootx_k2                     , "ootx.k2", PT_U16     , uint16_t,   2, 0x00, 5500, NULL, "") \
    P(ootx_k3                     , "ootx.k3", PT_U16     , uint16_t,   2, 0x00, 6500, NULL, "") \
    P(ootx_stretch                , "ootx.stretch", PT_BOOL    , bool    ,   1, 0x00, true, NULL, "") \
    P(mode_count                  , "mode.count", PT_U8      , uint8_t ,   1, 0x00, 3, NULL, "") \
    P(mode_current                , "mode.current", PT_U8      , uint8_t ,   1, 0x00, 0, NULL, "") \
    P(mode_0_period               , "mode.0.period", PT_U32     , uint32_t,   4, 0x00, 800000, NULL, "") \
    P(mode_1_period               , "mode.1.period", PT_U32     , uint32_t,   4, 0x00, 800000, NULL, "") \
    P(mode_2_period               , "mode.2.period", PT_U32     , uint32_t,   4, 0x00, 800000, NULL, "") \
    P(mode_3_period               , "mode.3.period", PT_U32     , uint32_t,   4, 0x00, 800000, NULL, "") \
    P(mode_4_period               , "mode.4.period", PT_U32     , uint32_t,   4, 0x00, 765053, NULL, "") \
    P(mode_5_period               , "mode.5.period", PT_U32     , uint32_t,   4, 0x00, 836543, NULL, "") \
    P(mode_6_period               , "mode.6.period", PT_U32     , uint32_t,   4, 0x00, 874755, NULL, "") \
    P(mode_7_period               , "mode.7.period", PT_U32     , uint32_t,   4, 0x00, 914713, NULL, "") \
    P(mode_0_timebase_mode        , "mode.0.timebase.mode", PT_ENUM    , uint8_t ,   1, 0x00, 3, enum_e5cc, "") \
    P(mode_1_timebase_mode        , "mode.1.timebase.mode", PT_ENUM    , uint8_t ,   1, 0x00, 0, enum_e5cc, "") \
    P(mode_2_timebase_mode        , "mode.2.timebase.mode", PT_ENUM    , uint8_t ,   1, 0x00, 4, enum_e5cc, "") \
    P(mode_3_timebase_mode        , "mode.3.timebase.mode", PT_ENUM    , uint8_t ,   1, 0x00, 1, enum_e5cc, "") \
    P(mode_4_timebase_mode        , "mode.4.timebase.mode", PT_ENUM    , uint8_t ,   1, 0x00, 2, enum_e5cc, "") \
    P(mode_5_timebase_mode        , "mode.5.timebase.mode", PT_ENUM    , uint8_t ,   1, 0x00, 2, enum_e5cc, "") \
    P(mode_6_timebase_mode        , "mode.6.timebase.mode", PT_ENUM    , uint8_t ,   1, 0x00, 2, enum_e5cc, "") \
    P(mode_7_timebase_mode        , "mode.7.timebase.mode", PT_ENUM    , uint8_t ,   1, 0x00, 2, enum_e5cc, "") \
    P(mode_0_timebase_offset      , "mode.0.timebase.offset", PT_I32     , int32_t ,   4, 0x00, 0, NULL, "") \
    P(mode_1_timebase_offset      , "mode.1.timebase.offset", PT_I32     , int32_t ,   4, 0x00, 20000, NULL, "") \
    P(mode_2_timebase_offset      , "mode.2.timebase.offset", PT_I32     , int32_t ,   4, 0x00, 0, NULL, "") \
    P(mode_3_timebase_offset      , "mode.3.timebase.offset", PT_I32     , int32_t ,   4, 0x00, 0, NULL, "") \
    P(mode_4_timebase_offset      , "mode.4.timebase.offset", PT_I32     , int32_t ,   4, 0x00, 0, NULL, "") \
    P(mode_5_timebase_offset      , "mode.5.timebase.offset", PT_I32     , int32_t ,   4, 0x00, 0, NULL, "") \
    P(mode_6_timebase_offset      , "mode.6.timebase.offset", PT_I32     , int32_t ,   4, 0x00, 0, NULL, "") \
    P(mode_7_timebase_offset      , "mode.7.timebase.offset", PT_I32     , int32_t ,   4, 0x00, 0, NULL, "") \
    P(mode_0_ootx                 , "mode.0.ootx", PT_ENUM    , uint8_t ,   1, 0x00, 1, enum_e580, "") \
    P(mode_1_ootx                 , "mode.1.ootx", PT_ENUM    , uint8_t ,   1, 0x00, 1, enum_e580, "") \
    P(mode_2_ootx                 , "mode.2.ootx", PT_ENUM    , uint8_t ,   1, 0x00, 1, enum_e580, "") \
    P(mode_3_ootx                 , "mode.3.ootx", PT_ENUM    , uint8_t ,   1, 0x00, 2, enum_e580, "") \
    P(mode_4_ootx                 , "mode.4.ootx", PT_ENUM    , uint8_t ,   1, 0x00, 0, enum_e580, "") \
    P(mode_5_ootx                 , "mode.5.ootx", PT_ENUM    , uint8_t ,   1, 0x00, 0, enum_e580, "") \
    P(mode_6_ootx                 , "mode.6.ootx", PT_ENUM    , uint8_t ,   1, 0x00, 0, enum_e580, "") \
    P(mode_7_ootx                 , "mode.7.ootx", PT_ENUM    , uint8_t ,   1, 0x00, 0, enum_e580, "") \
    P(mode_0_label                , "mode.0.label", PT_U8      , uint8_t ,   1, 0x00, 65, NULL, "") \
    P(mode_1_label                , "mode.1.label", PT_U8      , uint8_t ,   1, 0x00, 98, NULL, "") \
    P(mode_2_label                , "mode.2.label", PT_U8      , uint8_t ,   1, 0x00, 99, NULL, "") \
    P(mode_3_label                , "mode.3.label", PT_U8      , uint8_t ,   1, 0x00, 76, NULL, "") \
    P(mode_4_label                , "mode.4.label", PT_U8      , uint8_t ,   1, 0x00, 49, NULL, "") \
    P(mode_5_label                , "mode.5.label", PT_U8      , uint8_t ,   1, 0x00, 50, NULL, "") \
    P(mode_6_label                , "mode.6.label", PT_U8      , uint8_t ,   1, 0x00, 51, NULL, "") \
    P(mode_7_label                , "mode.7.label", PT_U8      , uint8_t ,   1, 0x00, 52, NULL, "") \
    P(mode_0_carrier              , "mode.0.carrier", PT_U32     , uint32_t,   4, 0x00, 1843200, NULL, "") \
    P(mode_1_carrier              , "mode.1.carrier", PT_U32     , uint32_t,   4, 0x00, 1843200, NULL, "") \
    P(mode_2_carrier              , "mode.2.carrier", PT_U32     , uint32_t,   4, 0x00, 1843200, NULL, "") \
    P(mode_3_carrier              , "mode.3.carrier", PT_U32     , uint32_t,   4, 0x00, 1843200, NULL, "") \
    P(mode_4_carrier              , "mode.4.carrier", PT_U32     , uint32_t,   4, 0x00, 1843200, NULL, "") \
    P(mode_5_carrier              , "mode.5.carrier", PT_U32     , uint32_t,   4, 0x00, 1843200, NULL, "") \
    P(mode_6_carrier              , "mode.6.carrier", PT_U32     , uint32_t,   4, 0x00, 1843200, NULL, "") \
    P(mode_7_carrier              , "mode.7.carrier", PT_U32     , uint32_t,   4, 0x00, 1843200, NULL, "") \
    P(accel_enable                , "accel.enable", PT_BOOL    , bool    ,   1, 0x00, true, NULL, "") \
    P(accel_debug                 , "accel.debug", PT_BOOL    , bool    ,   1, 0x00, false, NULL, "") \
    P(accel_b1                    , "accel.b1", PT_FLOAT   , float   ,   4, 0x00, 0.97f, NULL, "") \
    P(accel_x                     , "accel.x", PT_FLOAT   , float   ,   4, 0x01, 0.0f, NULL, "") \
    P(accel_y                     , "accel.y", PT_FLOAT   , float   ,   4, 0x01, 0.0f, NULL, "") \
    P(accel_z                     , "accel.z", PT_FLOAT   , float   ,   4, 0x01, 0.0f, NULL, "") \
    P(accel_magnitude             , "accel.magnitude", PT_FLOAT   , float   ,   4, 0x01, 0.0f, NULL, "") \
    P(accel_is_gravity            , "accel.is_gravity", PT_BOOL    , bool    ,   1, 0x01, false, NULL, "") \
    P(accel_dir_x                 , "accel.dir_x", PT_I8      , int8_t  ,   1, 0x01, 0, NULL, "") \
    P(accel_dir_y                 , "accel.dir_y", PT_I8      , int8_t  ,   1, 0x01, 0, NULL, "") \
    P(accel_dir_z                 , "accel.dir_z", PT_I8      , int8_t  ,   1, 0x01, 0, NULL, "") \
    P(sys_warnings                , "sys.warnings", PT_BOOL    , bool    ,   1, 0x00, false, NULL, "") \
    P(reserved0                   , "", PT_RESERVED, char    ,   4, 0x00, 0, NULL, "") \
    P(sys_emission_enable         , "sys.emission_enable", PT_BOOL    , bool    ,   1, 0x01, true, NULL, "") \
    P(sys_uptime                  , "sys.uptime", PT_U32     , uint32_t,   4, 0x01, 0, NULL, "") \
    P(sys_standby                 , "sys.standby", PT_BOOL    , bool    ,   1, 0x01, false, NULL, "") \
    P(sys_identify                , "sys.identify", PT_BOOL    , bool    ,   1, 0x01, false, NULL, "") \
    P(sys_faults                  , "sys.faults", PT_U8      , uint8_t ,   1, 0x01, 0, NULL, "") \
    P(laser_0_pwr_b2              , "laser.0.pwr.b2", PT_FLOAT   , float   ,   4, 0x00, 0.0f, NULL, "") \
    P(laser_1_pwr_b2              , "laser.1.pwr.b2", PT_FLOAT   , float   ,   4, 0x00, 0.0f, NULL, "") \
    P(reserved1                   , "", PT_RESERVED, char    , 120, 0x00, 0, NULL, "") \
    P(fcal_0_tilt                 , "fcal.0.tilt", PT_FLOAT   , float   ,   4, 0x02, 0.0f, NULL, "") \
    P(fcal_0_phase                , "fcal.0.phase", PT_FLOAT   , float   ,   4, 0x02, 0.0f, NULL, "") \
    P(fcal_0_curve                , "fcal.0.curve", PT_FLOAT   , float   ,   4, 0x02, 0.0f, NULL, "") \
    P(fcal_0_gibphase             , "fcal.0.gibphase", PT_FLOAT   , float   ,   4, 0x02, 0.0f, NULL, "") \
    P(fcal_0_gibmag               , "fcal.0.gibmag", PT_FLOAT   , float   ,   4, 0x02, 0.0f, NULL, "") \
    P(fcal_1_tilt                 , "fcal.1.tilt", PT_FLOAT   , float   ,   4, 0x02, 0.0f, NULL, "") \
    P(fcal_1_phase                , "fcal.1.phase", PT_FLOAT   , float   ,   4, 0x02, 0.0f, NULL, "") \
    P(fcal_1_curve                , "fcal.1.curve", PT_FLOAT   , float   ,   4, 0x02, 0.0f, NULL, "") \
    P(fcal_1_gibphase             , "fcal.1.gibphase", PT_FLOAT   , float   ,   4, 0x02, 0.0f, NULL, "") \
    P(fcal_1_gibmag               , "fcal.1.gibmag", PT_FLOAT   , float   ,   4, 0x02, 0.0f, NULL, "") \

#define ENUM_E5CC_NAMES "master-tdm", "master-sync", "master-async", "slave-cable", "slave-optical"
#define ENUM_E5E4_NAMES "internal", "cable", "optical"
#define ENUM_11E7C_NAMES "disabled", "searching", "syncing", "locked", "lost"
#define ENUM_E580_NAMES "short", "medium", "long"
