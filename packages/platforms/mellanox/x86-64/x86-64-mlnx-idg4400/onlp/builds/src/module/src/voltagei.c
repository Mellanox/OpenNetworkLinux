/************************************************************
 * <bsn.cl fy=2014 v=onl>
 *
 *           Copyright 2014 Big Switch Networks, Inc.
 *
 * Licensed under the Eclipse Public License, Version 1.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *        http://www.eclipse.org/legal/epl-v10.html
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the
 * License.
 *
 * </bsn.cl>
 ************************************************************
 *
 *
 *
 ***********************************************************/
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <onlplib/mmap.h>
#include <onlplib/file.h>
#include <onlp/platformi/voltagei.h>
#include "platform_lib.h"

#define prefix_path "/bsp/voltage"
#define driver_value_len 50


#define VALIDATE(_id)                           \
    do {                                        \
        if(!ONLP_OID_IS_VOLTAGE(_id)) {             \
            return ONLP_STATUS_E_INVALID;       \
        }                                       \
    } while(0)

// "psu2_vcap",

static char file_names[][20] =  /* must map with voltage_led_id */
{
    "reserved",
    "cpu_0_9",
    "cpu_1_05",
    "cpu_1_8",
    "cpu_pch",
    "ddr3_0_675",
    "ddr3_1_35",
    "lan",
    "psu2_vin",
    "psu2_vout",
    "sys",
    "usb",
    "vcore_vin",
    "vcore_vout1",
    "vcore_vout2",
    "vmon_vin",
    "vmon_vout"
};

/* Static values */
static onlp_voltage_info_t linfo[] = {
    { }, /* Not used */
    {
            { ONLP_VOLTAGE_ID_CREATE(VOLTAGE_CPU_0_9), "CPU 0.9V", 0},
            ONLP_VOLTAGE_STATUS_PRESENT,
            ONLP_VOLTAGE_CAPS_ALL, 0, {0, 0},
    },
    {
            { ONLP_VOLTAGE_ID_CREATE(VOLTAGE_CPU_1_05), "CPU 1.05V", 0},
            ONLP_VOLTAGE_STATUS_PRESENT,
            ONLP_VOLTAGE_CAPS_ALL, 0, {0, 0},
    },
    {
            { ONLP_VOLTAGE_ID_CREATE(VOLTAGE_CPU_1_8), "CPU 1.8V", 0},
            ONLP_VOLTAGE_STATUS_PRESENT,
            ONLP_VOLTAGE_CAPS_ALL, 0, {0, 0},
    },
    {
            { ONLP_VOLTAGE_ID_CREATE(VOLTAGE_CPU_PCH), "CPU/PCH 1.05V", 0},
            ONLP_VOLTAGE_STATUS_PRESENT,
            ONLP_VOLTAGE_CAPS_ALL, 0, {0, 0}
    },
    {
            { ONLP_VOLTAGE_ID_CREATE(VOLTAGE_DDR3_0_675), "DDR3 0.675V", 0},
            ONLP_VOLTAGE_STATUS_PRESENT,
            ONLP_VOLTAGE_CAPS_ALL, 0, {0, 0}
    },
    {
            { ONLP_VOLTAGE_ID_CREATE(VOLTAGE_DDR3_1_35), "DDR3 1.35V", 0},
            ONLP_VOLTAGE_STATUS_PRESENT,
            ONLP_VOLTAGE_CAPS_ALL, 0, {0, 0}
    },
    {
            { ONLP_VOLTAGE_ID_CREATE(VOLTAGE_LAN), "1.05V LAN", 0},
            ONLP_VOLTAGE_STATUS_PRESENT,
            ONLP_VOLTAGE_CAPS_ALL, 0, {0, 0}
    },
    {
            { ONLP_VOLTAGE_ID_CREATE(VOLTAGE_PSU2_VIN), "PSU2 Voltage In", 0},
            ONLP_VOLTAGE_STATUS_PRESENT,
            ONLP_VOLTAGE_CAPS_ALL, 0, {0, 0}
    },
    {
            { ONLP_VOLTAGE_ID_CREATE(VOLTAGE_PSU2_VOUT), "PSU2 Voltage Out", 0},
            ONLP_VOLTAGE_STATUS_PRESENT,
            ONLP_VOLTAGE_CAPS_ALL, 0, {0, 0}
    },
    {
            { ONLP_VOLTAGE_ID_CREATE(VOLTAGE_SYS), "SYS 3.3V", 0},
            ONLP_VOLTAGE_STATUS_PRESENT,
            ONLP_VOLTAGE_CAPS_ALL, 0, {0, 0}
    },
    {
            { ONLP_VOLTAGE_ID_CREATE(VOLTAGE_USB), "USB 5V", 0},
            ONLP_VOLTAGE_STATUS_PRESENT,
            ONLP_VOLTAGE_CAPS_ALL, 0, {0, 0}
    },
    {
            { ONLP_VOLTAGE_ID_CREATE(VOLTAGE_VCORE_VIN), "Vcore Voltage In", 0},
            ONLP_VOLTAGE_STATUS_PRESENT,
            ONLP_VOLTAGE_CAPS_ALL, 0, {0, 0}
    },
    {
            { ONLP_VOLTAGE_ID_CREATE(VOLTAGE_VCORE_VOUT1), "Vcore Voltage Out1", 0},
            ONLP_VOLTAGE_STATUS_PRESENT,
            ONLP_VOLTAGE_CAPS_ALL, 0, {0, 0}
    },
    {
            { ONLP_VOLTAGE_ID_CREATE(VOLTAGE_VCORE_VOUT2), "Vcore Voltage Out2", 0},
            ONLP_VOLTAGE_STATUS_PRESENT,
            ONLP_VOLTAGE_CAPS_ALL, 0, {0, 0}
    },
    {
            { ONLP_VOLTAGE_ID_CREATE(VOLTAGE_VMON_VIN), "VMon Voltage In", 0},
            ONLP_VOLTAGE_STATUS_PRESENT,
            ONLP_VOLTAGE_CAPS_ALL, 0, {0, 0}
    },
    {
            { ONLP_VOLTAGE_ID_CREATE(VOLTAGE_VMON_VOUT), "VMon Voltage Out", 0},
            ONLP_VOLTAGE_STATUS_PRESENT,
            ONLP_VOLTAGE_CAPS_ALL, 0, {0, 0}
    },

};

/*
 * This function will be called prior to any other onlp_voltagei_* functions.
 */
int
onlp_voltagei_init(void)
{
    return ONLP_STATUS_OK;
}

int
onlp_voltagei_info_get(onlp_oid_t id, onlp_voltage_info_t* info)
{
    int  len, local_id = 0;
    char data[driver_value_len] = {0};

    VALIDATE(id);

    local_id = ONLP_OID_ID_GET(id);
    *info = linfo[local_id];

    /* Get Voltage value */
    if (onlp_file_read((uint8_t*)data, sizeof(data), &len, "%s/%s_in",
            prefix_path, file_names[local_id]) != 0) {
        return ONLP_STATUS_E_INTERNAL;
    }

    info->voltage = atoi(data);

    /* Get Voltage min value */
    if (onlp_file_read((uint8_t*)data, sizeof(data), &len, "%s/%s_min",
            prefix_path, file_names[local_id]) != 0) {
        return ONLP_STATUS_E_INTERNAL;
    }

    info->range.min = atoi(data);
    /* Get Voltage max value */
    if (onlp_file_read((uint8_t*)data, sizeof(data), &len, "%s/%s_max",
            prefix_path, file_names[local_id]) != 0) {
        return ONLP_STATUS_E_INTERNAL;
    }

    info->range.max = atoi(data);

    return ONLP_STATUS_OK;
}


