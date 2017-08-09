/************************************************************
 * <bsn.cl fy=2014 v=onl>
 *
 *        Copyright 2014, 2015 Big Switch Networks, Inc.
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
 * Voltage Sensor Management.
 *
 ************************************************************/
#ifndef __ONLP_VOLTAGE_H__
#define __ONLP_VOLTAGE_H__

#include <onlp/onlp_config.h>
#include <onlp/onlp.h>
#include <onlp/oids.h>


/** onlp_voltage_status */
typedef enum onlp_voltage_status_e {
    ONLP_VOLTAGE_STATUS_PRESENT = (1 << 0),
    ONLP_VOLTAGE_STATUS_FAILED = (1 << 1),
} onlp_voltage_status_t;


/**
 * Shortcut for specifying all capabilties.
 */
#define ONLP_VOLTAGE_CAPS_ALL 0xF

/**
 * Voltage sensor information structure.
 */
typedef struct onlp_voltage_info_s {

    /** OID Header */
    onlp_oid_hdr_t hdr;

    /** Status */
    uint32_t status;

    /** Capabilities */
    uint32_t caps;

    /* Current voltage  */
    int voltage;

    struct {
        /* Min voltage value */
        int min;

        /* Max voltage value */
        int max;
    } range;

} onlp_voltage_info_t;

/**
 * @brief Initialize the voltage subsystem.
 */
int onlp_voltage_init(void);

/**
 * @brief Retrieve information about the given voltage id.
 * @param id The voltage oid.
 * @param rv [out] Receives the voltage information.
 */
int onlp_voltage_info_get(onlp_oid_t id, onlp_voltage_info_t* rv);

/**
 * @brief Retrieve the voltage's operational status.
 * @param id The voltage oid.
 * @param rv [out] Receives the operational status.
 */
int onlp_voltage_status_get(onlp_oid_t id, uint32_t* rv);

/**
 * @brief Retrieve the voltage's oid header.
 * @param id The voltage oid.
 * @param rv [out] Receives the header.
 */
int onlp_voltage_hdr_get(onlp_oid_t id, onlp_oid_hdr_t* rv);


/**
 * @brief Voltage OID debug dump.
 * @param id The voltage id.
 * @param pvs The output pvs.
 * @param flags The dump flags.
 */
void onlp_voltage_dump(onlp_oid_t id, aim_pvs_t* pvs, uint32_t flags);

/**
 * @brief Show the given voltage OID.
 * @param id The Voltage OID
 * @param pvs The output pvs
 * @param flags The output flags
 */
void onlp_voltage_show(onlp_oid_t id, aim_pvs_t* pvs, uint32_t flags);


#endif /* __ONLP_VOLTAGE_H__ */
