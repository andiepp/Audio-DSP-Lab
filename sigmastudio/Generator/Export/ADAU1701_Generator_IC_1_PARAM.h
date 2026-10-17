/*
 * File:           H:\Development-Lab\AudioMiniLab\sigmastudio\Generator\ADAU1701_Generator_IC_1_PARAM.h
 *
 * Created:        Saturday, October 10, 2026 3:35:34 PM
 * Description:    ADAU1701_Generator:IC 1 parameter RAM definitions.
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
#ifndef __ADAU1701_GENERATOR_IC_1_PARAM_H__
#define __ADAU1701_GENERATOR_IC_1_PARAM_H__


/* Module Noise - White Noise*/
#define MOD_NOISE_COUNT                                3
#define MOD_NOISE_DEVICE                               "IC1"
#define MOD_NOISE_ALG0_ENABLENOISE_ADDR                0
#define MOD_NOISE_ALG0_ENABLENOISE_FIXPT               0x00800000
#define MOD_NOISE_ALG0_ENABLENOISE_VALUE               SIGMASTUDIOTYPE_FIXPOINT_CONVERT(1)
#define MOD_NOISE_ALG0_ENABLENOISE_TYPE                SIGMASTUDIOTYPE_FIXPOINT
#define MOD_NOISE_ALG0_SEED_ADDR                       1
#define MOD_NOISE_ALG0_SEED_FIXPT                      0x0005464B
#define MOD_NOISE_ALG0_SEED_VALUE                      SIGMASTUDIOTYPE_INTEGER_CONVERT(345675)
#define MOD_NOISE_ALG0_SEED_TYPE                       SIGMASTUDIOTYPE_INTEGER
#define MOD_NOISE_ALG0_SEED_ADDR                       1
#define MOD_NOISE_ALG0_SEED_FIXPT                      0x0005464B
#define MOD_NOISE_ALG0_SEED_VALUE                      SIGMASTUDIOTYPE_INTEGER_CONVERT(345675)
#define MOD_NOISE_ALG0_SEED_TYPE                       SIGMASTUDIOTYPE_INTEGER

/* Module Triangle - Triangle wave*/
#define MOD_TRIANGLE_COUNT                             7
#define MOD_TRIANGLE_DEVICE                            "IC1"
#define MOD_TRIANGLE_ALG0_TRI0_ADDR                    2
#define MOD_TRIANGLE_ALG0_TRI0_FIXPT                   0x00000000
#define MOD_TRIANGLE_ALG0_TRI0_VALUE                   SIGMASTUDIOTYPE_FIXPOINT_CONVERT(0)
#define MOD_TRIANGLE_ALG0_TRI0_TYPE                    SIGMASTUDIOTYPE_FIXPOINT
#define MOD_TRIANGLE_ALG0_TRI1_ADDR                    3
#define MOD_TRIANGLE_ALG0_TRI1_FIXPT                   0x00800000
#define MOD_TRIANGLE_ALG0_TRI1_VALUE                   SIGMASTUDIOTYPE_FIXPOINT_CONVERT(1)
#define MOD_TRIANGLE_ALG0_TRI1_TYPE                    SIGMASTUDIOTYPE_FIXPOINT
#define MOD_TRIANGLE_ALG0_TRI2_ADDR                    4
#define MOD_TRIANGLE_ALG0_TRI2_FIXPT                   0x00000000
#define MOD_TRIANGLE_ALG0_TRI2_VALUE                   SIGMASTUDIOTYPE_FIXPOINT_CONVERT(0)
#define MOD_TRIANGLE_ALG0_TRI2_TYPE                    SIGMASTUDIOTYPE_FIXPOINT
#define MOD_TRIANGLE_ALG0_TRI3_ADDR                    5
#define MOD_TRIANGLE_ALG0_TRI3_FIXPT                   0xFF800000
#define MOD_TRIANGLE_ALG0_TRI3_VALUE                   SIGMASTUDIOTYPE_FIXPOINT_CONVERT(-1)
#define MOD_TRIANGLE_ALG0_TRI3_TYPE                    SIGMASTUDIOTYPE_FIXPOINT
#define MOD_STATIC_TRIANGLE_ALG0_MASK_ADDR             6
#define MOD_STATIC_TRIANGLE_ALG0_MASK_FIXPT            0x00000003
#define MOD_STATIC_TRIANGLE_ALG0_MASK_VALUE            SIGMASTUDIOTYPE_INTEGER_CONVERT(3)
#define MOD_STATIC_TRIANGLE_ALG0_MASK_TYPE             SIGMASTUDIOTYPE_INTEGER
#define MOD_TRIANGLE_ALG0_FREQ_ADDR                    7
#define MOD_TRIANGLE_ALG0_FREQ_FIXPT                   0x000553F7
#define MOD_TRIANGLE_ALG0_FREQ_VALUE                   SIGMASTUDIOTYPE_FIXPOINT_CONVERT(0.041625)
#define MOD_TRIANGLE_ALG0_FREQ_TYPE                    SIGMASTUDIOTYPE_FIXPOINT
#define MOD_TRIANGLE_ALG0_ON_ADDR                      8
#define MOD_TRIANGLE_ALG0_ON_FIXPT                     0x00800000
#define MOD_TRIANGLE_ALG0_ON_VALUE                     SIGMASTUDIOTYPE_FIXPOINT_CONVERT(1)
#define MOD_TRIANGLE_ALG0_ON_TYPE                      SIGMASTUDIOTYPE_FIXPOINT

/* Module Square - Square wave*/
#define MOD_SQUARE_COUNT                               3
#define MOD_SQUARE_DEVICE                              "IC1"
#define MOD_STATIC_SQUARE_ALG0_MASK_ADDR               9
#define MOD_STATIC_SQUARE_ALG0_MASK_FIXPT              0x000000FF
#define MOD_STATIC_SQUARE_ALG0_MASK_VALUE              SIGMASTUDIOTYPE_INTEGER_CONVERT(255)
#define MOD_STATIC_SQUARE_ALG0_MASK_TYPE               SIGMASTUDIOTYPE_INTEGER
#define MOD_SQUARE_ALG0_FREQ_ADDR                      10
#define MOD_SQUARE_ALG0_FREQ_FIXPT                     0x00054FDF
#define MOD_SQUARE_ALG0_FREQ_VALUE                     SIGMASTUDIOTYPE_FIXPOINT_CONVERT(0.0415)
#define MOD_SQUARE_ALG0_FREQ_TYPE                      SIGMASTUDIOTYPE_FIXPOINT
#define MOD_SQUARE_ALG0_ON_ADDR                        11
#define MOD_SQUARE_ALG0_ON_FIXPT                       0x00800000
#define MOD_SQUARE_ALG0_ON_VALUE                       SIGMASTUDIOTYPE_FIXPOINT_CONVERT(1)
#define MOD_SQUARE_ALG0_ON_TYPE                        SIGMASTUDIOTYPE_FIXPOINT

/* Module Sine - Sine Tone*/
#define MOD_SINE_COUNT                                 3
#define MOD_SINE_DEVICE                                "IC1"
#define MOD_STATIC_SINE_ALG0_MASK_ADDR                 12
#define MOD_STATIC_SINE_ALG0_MASK_FIXPT                0x000000FF
#define MOD_STATIC_SINE_ALG0_MASK_VALUE                SIGMASTUDIOTYPE_INTEGER_CONVERT(255)
#define MOD_STATIC_SINE_ALG0_MASK_TYPE                 SIGMASTUDIOTYPE_INTEGER
#define MOD_SINE_ALG0_INCREMENT_ADDR                   13
#define MOD_SINE_ALG0_INCREMENT_FIXPT                  0x00055555
#define MOD_SINE_ALG0_INCREMENT_VALUE                  SIGMASTUDIOTYPE_FIXPOINT_CONVERT(0.0416666666666667)
#define MOD_SINE_ALG0_INCREMENT_TYPE                   SIGMASTUDIOTYPE_FIXPOINT
#define MOD_SINE_ALG0_ON_ADDR                          14
#define MOD_SINE_ALG0_ON_FIXPT                         0x00800000
#define MOD_SINE_ALG0_ON_VALUE                         SIGMASTUDIOTYPE_FIXPOINT_CONVERT(1)
#define MOD_SINE_ALG0_ON_TYPE                          SIGMASTUDIOTYPE_FIXPOINT

/* Module Waveform - Mono Switch Nx1*/
#define MOD_WAVEFORM_COUNT                             1
#define MOD_WAVEFORM_DEVICE                            "IC1"
#define MOD_WAVEFORM_MONOSWSLEW_ADDR                   15
#define MOD_WAVEFORM_MONOSWSLEW_FIXPT                  0x00000002
#define MOD_WAVEFORM_MONOSWSLEW_VALUE                  SIGMASTUDIOTYPE_INTEGER_CONVERT(2)
#define MOD_WAVEFORM_MONOSWSLEW_TYPE                   SIGMASTUDIOTYPE_INTEGER

/* Module Amplitude - Single SW slew vol (adjustable)*/
#define MOD_AMPLITUDE_COUNT                            2
#define MOD_AMPLITUDE_DEVICE                           "IC1"
#define MOD_AMPLITUDE_ALG0_TARGET_ADDR                 16
#define MOD_AMPLITUDE_ALG0_TARGET_FIXPT                0x000CCCCC
#define MOD_AMPLITUDE_ALG0_TARGET_VALUE                SIGMASTUDIOTYPE_FIXPOINT_CONVERT(0.1)
#define MOD_AMPLITUDE_ALG0_TARGET_TYPE                 SIGMASTUDIOTYPE_FIXPOINT
#define MOD_AMPLITUDE_ALG0_STEP_ADDR                   17
#define MOD_AMPLITUDE_ALG0_STEP_FIXPT                  0x00000800
#define MOD_AMPLITUDE_ALG0_STEP_VALUE                  SIGMASTUDIOTYPE_FIXPOINT_CONVERT(0.000244140625)
#define MOD_AMPLITUDE_ALG0_STEP_TYPE                   SIGMASTUDIOTYPE_FIXPOINT

#endif
