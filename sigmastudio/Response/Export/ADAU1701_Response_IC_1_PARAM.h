/*
 * File:           H:\Development-Lab\AudioMiniLab\sigmastudio\Response\Export\ADAU1701_Response_IC_1_PARAM.h
 *
 * Created:        Monday, October 12, 2026 9:26:14 AM
 * Description:    ADAU1701_Response:IC 1 parameter RAM definitions.
 *
 * This software is distributed in the hope that it will be useful,
 * but is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * This software may only be used to program products purchased from
 * Analog Devices for incorporation by you into audio products that
 * are intended for resale to audio product end users. This software
 * may not be distributed whole or in any part to third parties.
 *
 * Copyright ©2026 Analog Devices, Inc. All rights reserved.
 */
#ifndef __ADAU1701_RESPONSE_IC_1_PARAM_H__
#define __ADAU1701_RESPONSE_IC_1_PARAM_H__


/* Module Sine - Sine Tone*/
#define MOD_SINE_COUNT                                 3
#define MOD_SINE_DEVICE                                "IC1"
#define MOD_STATIC_SINE_ALG0_MASK_ADDR                 0
#define MOD_STATIC_SINE_ALG0_MASK_FIXPT                0x000000FF
#define MOD_STATIC_SINE_ALG0_MASK_VALUE                SIGMASTUDIOTYPE_INTEGER_CONVERT(255)
#define MOD_STATIC_SINE_ALG0_MASK_TYPE                 SIGMASTUDIOTYPE_INTEGER
#define MOD_SINE_ALG0_INCREMENT_ADDR                   1
#define MOD_SINE_ALG0_INCREMENT_FIXPT                  0x00055555
#define MOD_SINE_ALG0_INCREMENT_VALUE                  SIGMASTUDIOTYPE_FIXPOINT_CONVERT(0.0416666666666667)
#define MOD_SINE_ALG0_INCREMENT_TYPE                   SIGMASTUDIOTYPE_FIXPOINT
#define MOD_SINE_ALG0_ON_ADDR                          2
#define MOD_SINE_ALG0_ON_FIXPT                         0x00800000
#define MOD_SINE_ALG0_ON_VALUE                         SIGMASTUDIOTYPE_FIXPOINT_CONVERT(1)
#define MOD_SINE_ALG0_ON_TYPE                          SIGMASTUDIOTYPE_FIXPOINT

/* Module Amplitude - Single SW slew vol (adjustable)*/
#define MOD_AMPLITUDE_COUNT                            2
#define MOD_AMPLITUDE_DEVICE                           "IC1"
#define MOD_AMPLITUDE_ALG0_TARGET_ADDR                 3
#define MOD_AMPLITUDE_ALG0_TARGET_FIXPT                0x000CCCCC
#define MOD_AMPLITUDE_ALG0_TARGET_VALUE                SIGMASTUDIOTYPE_FIXPOINT_CONVERT(0.1)
#define MOD_AMPLITUDE_ALG0_TARGET_TYPE                 SIGMASTUDIOTYPE_FIXPOINT
#define MOD_AMPLITUDE_ALG0_STEP_ADDR                   4
#define MOD_AMPLITUDE_ALG0_STEP_FIXPT                  0x00000800
#define MOD_AMPLITUDE_ALG0_STEP_VALUE                  SIGMASTUDIOTYPE_FIXPOINT_CONVERT(0.000244140625)
#define MOD_AMPLITUDE_ALG0_STEP_TYPE                   SIGMASTUDIOTYPE_FIXPOINT

/* Module BandL - General (2nd order)*/
#define MOD_BANDL_COUNT                                5
#define MOD_BANDL_DEVICE                               "IC1"
#define MOD_BANDL_ALG0_STAGE0_B0_ADDR                  5
#define MOD_BANDL_ALG0_STAGE0_B0_FIXPT                 0x00800000
#define MOD_BANDL_ALG0_STAGE0_B0_VALUE                 SIGMASTUDIOTYPE_FIXPOINT_CONVERT(1)
#define MOD_BANDL_ALG0_STAGE0_B0_TYPE                  SIGMASTUDIOTYPE_FIXPOINT
#define MOD_BANDL_ALG0_STAGE0_B1_ADDR                  6
#define MOD_BANDL_ALG0_STAGE0_B1_FIXPT                 0x00000000
#define MOD_BANDL_ALG0_STAGE0_B1_VALUE                 SIGMASTUDIOTYPE_FIXPOINT_CONVERT(0)
#define MOD_BANDL_ALG0_STAGE0_B1_TYPE                  SIGMASTUDIOTYPE_FIXPOINT
#define MOD_BANDL_ALG0_STAGE0_B2_ADDR                  7
#define MOD_BANDL_ALG0_STAGE0_B2_FIXPT                 0x00000000
#define MOD_BANDL_ALG0_STAGE0_B2_VALUE                 SIGMASTUDIOTYPE_FIXPOINT_CONVERT(0)
#define MOD_BANDL_ALG0_STAGE0_B2_TYPE                  SIGMASTUDIOTYPE_FIXPOINT
#define MOD_BANDL_ALG0_STAGE0_A0_ADDR                  8
#define MOD_BANDL_ALG0_STAGE0_A0_FIXPT                 0x00000000
#define MOD_BANDL_ALG0_STAGE0_A0_VALUE                 SIGMASTUDIOTYPE_FIXPOINT_CONVERT(0)
#define MOD_BANDL_ALG0_STAGE0_A0_TYPE                  SIGMASTUDIOTYPE_FIXPOINT
#define MOD_BANDL_ALG0_STAGE0_A1_ADDR                  9
#define MOD_BANDL_ALG0_STAGE0_A1_FIXPT                 0x00000000
#define MOD_BANDL_ALG0_STAGE0_A1_VALUE                 SIGMASTUDIOTYPE_FIXPOINT_CONVERT(0)
#define MOD_BANDL_ALG0_STAGE0_A1_TYPE                  SIGMASTUDIOTYPE_FIXPOINT

/* Module BandR - General (2nd order)*/
#define MOD_BANDR_COUNT                                5
#define MOD_BANDR_DEVICE                               "IC1"
#define MOD_BANDR_ALG0_STAGE0_B0_ADDR                  10
#define MOD_BANDR_ALG0_STAGE0_B0_FIXPT                 0x00800000
#define MOD_BANDR_ALG0_STAGE0_B0_VALUE                 SIGMASTUDIOTYPE_FIXPOINT_CONVERT(1)
#define MOD_BANDR_ALG0_STAGE0_B0_TYPE                  SIGMASTUDIOTYPE_FIXPOINT
#define MOD_BANDR_ALG0_STAGE0_B1_ADDR                  11
#define MOD_BANDR_ALG0_STAGE0_B1_FIXPT                 0x00000000
#define MOD_BANDR_ALG0_STAGE0_B1_VALUE                 SIGMASTUDIOTYPE_FIXPOINT_CONVERT(0)
#define MOD_BANDR_ALG0_STAGE0_B1_TYPE                  SIGMASTUDIOTYPE_FIXPOINT
#define MOD_BANDR_ALG0_STAGE0_B2_ADDR                  12
#define MOD_BANDR_ALG0_STAGE0_B2_FIXPT                 0x00000000
#define MOD_BANDR_ALG0_STAGE0_B2_VALUE                 SIGMASTUDIOTYPE_FIXPOINT_CONVERT(0)
#define MOD_BANDR_ALG0_STAGE0_B2_TYPE                  SIGMASTUDIOTYPE_FIXPOINT
#define MOD_BANDR_ALG0_STAGE0_A0_ADDR                  13
#define MOD_BANDR_ALG0_STAGE0_A0_FIXPT                 0x00000000
#define MOD_BANDR_ALG0_STAGE0_A0_VALUE                 SIGMASTUDIOTYPE_FIXPOINT_CONVERT(0)
#define MOD_BANDR_ALG0_STAGE0_A0_TYPE                  SIGMASTUDIOTYPE_FIXPOINT
#define MOD_BANDR_ALG0_STAGE0_A1_ADDR                  14
#define MOD_BANDR_ALG0_STAGE0_A1_FIXPT                 0x00000000
#define MOD_BANDR_ALG0_STAGE0_A1_VALUE                 SIGMASTUDIOTYPE_FIXPOINT_CONVERT(0)
#define MOD_BANDR_ALG0_STAGE0_A1_TYPE                  SIGMASTUDIOTYPE_FIXPOINT

/* Module SmoothL - General (2nd order)*/
#define MOD_SMOOTHL_COUNT                              5
#define MOD_SMOOTHL_DEVICE                             "IC1"
#define MOD_SMOOTHL_ALG0_STAGE0_B0_ADDR                15
#define MOD_SMOOTHL_ALG0_STAGE0_B0_FIXPT               0x00000003
#define MOD_SMOOTHL_ALG0_STAGE0_B0_VALUE               SIGMASTUDIOTYPE_FIXPOINT_CONVERT(4.64328307203556E-07)
#define MOD_SMOOTHL_ALG0_STAGE0_B0_TYPE                SIGMASTUDIOTYPE_FIXPOINT
#define MOD_SMOOTHL_ALG0_STAGE0_B1_ADDR                16
#define MOD_SMOOTHL_ALG0_STAGE0_B1_FIXPT               0x00000007
#define MOD_SMOOTHL_ALG0_STAGE0_B1_VALUE               SIGMASTUDIOTYPE_FIXPOINT_CONVERT(9.28656614407112E-07)
#define MOD_SMOOTHL_ALG0_STAGE0_B1_TYPE                SIGMASTUDIOTYPE_FIXPOINT
#define MOD_SMOOTHL_ALG0_STAGE0_B2_ADDR                17
#define MOD_SMOOTHL_ALG0_STAGE0_B2_FIXPT               0x00000003
#define MOD_SMOOTHL_ALG0_STAGE0_B2_VALUE               SIGMASTUDIOTYPE_FIXPOINT_CONVERT(4.64328307203556E-07)
#define MOD_SMOOTHL_ALG0_STAGE0_B2_TYPE                SIGMASTUDIOTYPE_FIXPOINT
#define MOD_SMOOTHL_ALG0_STAGE0_A0_ADDR                18
#define MOD_SMOOTHL_ALG0_STAGE0_A0_FIXPT               0x00FFB5BC
#define MOD_SMOOTHL_ALG0_STAGE0_A0_VALUE               SIGMASTUDIOTYPE_FIXPOINT_CONVERT(1.99773360720734)
#define MOD_SMOOTHL_ALG0_STAGE0_A0_TYPE                SIGMASTUDIOTYPE_FIXPOINT
#define MOD_SMOOTHL_ALG0_STAGE0_A1_ADDR                19
#define MOD_SMOOTHL_ALG0_STAGE0_A1_FIXPT               0xFF804A36
#define MOD_SMOOTHL_ALG0_STAGE0_A1_VALUE               SIGMASTUDIOTYPE_FIXPOINT_CONVERT(-0.997735318739846)
#define MOD_SMOOTHL_ALG0_STAGE0_A1_TYPE                SIGMASTUDIOTYPE_FIXPOINT

/* Module SmoothR - General (2nd order)*/
#define MOD_SMOOTHR_COUNT                              5
#define MOD_SMOOTHR_DEVICE                             "IC1"
#define MOD_SMOOTHR_ALG0_STAGE0_B0_ADDR                20
#define MOD_SMOOTHR_ALG0_STAGE0_B0_FIXPT               0x00000003
#define MOD_SMOOTHR_ALG0_STAGE0_B0_VALUE               SIGMASTUDIOTYPE_FIXPOINT_CONVERT(4.64328307203556E-07)
#define MOD_SMOOTHR_ALG0_STAGE0_B0_TYPE                SIGMASTUDIOTYPE_FIXPOINT
#define MOD_SMOOTHR_ALG0_STAGE0_B1_ADDR                21
#define MOD_SMOOTHR_ALG0_STAGE0_B1_FIXPT               0x00000007
#define MOD_SMOOTHR_ALG0_STAGE0_B1_VALUE               SIGMASTUDIOTYPE_FIXPOINT_CONVERT(9.28656614407112E-07)
#define MOD_SMOOTHR_ALG0_STAGE0_B1_TYPE                SIGMASTUDIOTYPE_FIXPOINT
#define MOD_SMOOTHR_ALG0_STAGE0_B2_ADDR                22
#define MOD_SMOOTHR_ALG0_STAGE0_B2_FIXPT               0x00000003
#define MOD_SMOOTHR_ALG0_STAGE0_B2_VALUE               SIGMASTUDIOTYPE_FIXPOINT_CONVERT(4.64328307203556E-07)
#define MOD_SMOOTHR_ALG0_STAGE0_B2_TYPE                SIGMASTUDIOTYPE_FIXPOINT
#define MOD_SMOOTHR_ALG0_STAGE0_A0_ADDR                23
#define MOD_SMOOTHR_ALG0_STAGE0_A0_FIXPT               0x00FFB5BC
#define MOD_SMOOTHR_ALG0_STAGE0_A0_VALUE               SIGMASTUDIOTYPE_FIXPOINT_CONVERT(1.99773360720734)
#define MOD_SMOOTHR_ALG0_STAGE0_A0_TYPE                SIGMASTUDIOTYPE_FIXPOINT
#define MOD_SMOOTHR_ALG0_STAGE0_A1_ADDR                24
#define MOD_SMOOTHR_ALG0_STAGE0_A1_FIXPT               0xFF804A36
#define MOD_SMOOTHR_ALG0_STAGE0_A1_VALUE               SIGMASTUDIOTYPE_FIXPOINT_CONVERT(-0.997735318739846)
#define MOD_SMOOTHR_ALG0_STAGE0_A1_TYPE                SIGMASTUDIOTYPE_FIXPOINT

/* Module LevelR - DSP Readback*/
#define MOD_LEVELR_COUNT                               2
#define MOD_LEVELR_DEVICE                              "IC1"
#define MOD_LEVELR_ALG0_VAL0_ADDR                      2074
#define MOD_LEVELR_ALG0_VAL0_VALUES                    SIGMASTUDIOTYPE_SPECIAL(0x0186)
#define MOD_LEVELR_ALG0_VAL0_TYPE                      SIGMASTUDIOTYPE_SPECIAL
#define MOD_LEVELR_ALG0_VAL0_READBACK_ADDR             0
#define MOD_LEVELR_ALG0_VAL1_ADDR                      2074
#define MOD_LEVELR_ALG0_VAL1_VALUE                     SIGMASTUDIOTYPE_5_19_CONVERT(0)
#define MOD_LEVELR_ALG0_VAL1_TYPE                      SIGMASTUDIOTYPE_5_19

/* Module LevelL - DSP Readback*/
#define MOD_LEVELL_COUNT                               2
#define MOD_LEVELL_DEVICE                              "IC1"
#define MOD_LEVELL_ALG0_VAL0_ADDR                      2074
#define MOD_LEVELL_ALG0_VAL0_VALUES                    SIGMASTUDIOTYPE_SPECIAL(0x017A)
#define MOD_LEVELL_ALG0_VAL0_TYPE                      SIGMASTUDIOTYPE_SPECIAL
#define MOD_LEVELL_ALG0_VAL0_READBACK_ADDR             0
#define MOD_LEVELL_ALG0_VAL1_ADDR                      2074
#define MOD_LEVELL_ALG0_VAL1_VALUE                     SIGMASTUDIOTYPE_5_19_CONVERT(0)
#define MOD_LEVELL_ALG0_VAL1_TYPE                      SIGMASTUDIOTYPE_5_19

#endif
