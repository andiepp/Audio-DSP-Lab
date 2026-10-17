/*
 * File:           H:\Development-Lab\AudioMiniLab\sigmastudio\SigmaStudio\Export\ADAU1701_SigmaStudio_IC_1_PARAM.h
 *
 * Created:        Monday, October 12, 2026 1:47:33 PM
 * Description:    ADAU1701_SigmaStudio:IC 1 parameter RAM definitions.
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
#ifndef __ADAU1701_TEST_IC_1_PARAM_H__
#define __ADAU1701_TEST_IC_1_PARAM_H__


/* Module VCO1 - VCO*/
#define MOD_VCO1_COUNT                                 1
#define MOD_VCO1_DEVICE                                "IC1"
#define MOD_VCO1_VCOALG1MASK_ADDR                      0
#define MOD_VCO1_VCOALG1MASK_FIXPT                     0x000000FF
#define MOD_VCO1_VCOALG1MASK_VALUE                     SIGMASTUDIOTYPE_INTEGER_CONVERT(255)
#define MOD_VCO1_VCOALG1MASK_TYPE                      SIGMASTUDIOTYPE_INTEGER

/* Module Amplitude - Single SW slew vol (adjustable)*/
#define MOD_AMPLITUDE_COUNT                            2
#define MOD_AMPLITUDE_DEVICE                           "IC1"
#define MOD_AMPLITUDE_ALG0_TARGET_ADDR                 1
#define MOD_AMPLITUDE_ALG0_TARGET_FIXPT                0x002026F3
#define MOD_AMPLITUDE_ALG0_TARGET_VALUE                SIGMASTUDIOTYPE_FIXPOINT_CONVERT(0.251188643150958)
#define MOD_AMPLITUDE_ALG0_TARGET_TYPE                 SIGMASTUDIOTYPE_FIXPOINT
#define MOD_AMPLITUDE_ALG0_STEP_ADDR                   2
#define MOD_AMPLITUDE_ALG0_STEP_FIXPT                  0x00000800
#define MOD_AMPLITUDE_ALG0_STEP_VALUE                  SIGMASTUDIOTYPE_FIXPOINT_CONVERT(0.000244140625)
#define MOD_AMPLITUDE_ALG0_STEP_TYPE                   SIGMASTUDIOTYPE_FIXPOINT

#endif
