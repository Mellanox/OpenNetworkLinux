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
 * Voltage Platform Implementation.
 *
 ***********************************************************/
#ifndef __ONLP_VOLTAGEI_H__
#define __ONLP_VOLTAGEI_H__

#include <onlp/voltage.h>

/**
 * @brief Initialize the voltage platform subsystem.
 */
int onlp_voltagei_init(void);

/**
 * @brief Get the information structure for the given voltage OID.
 * @param id The voltage OID
 * @param rv [out] Receives the voltage information.
 */
int onlp_voltagei_info_get(onlp_oid_t id, onlp_voltage_info_t* rv);

/**
 * @brief Retrieve the voltage's operational status.
 * @param id The voltage OID.
 * @param rv [out] Receives the voltage's operations status flags.
 * @notes Only operational state needs to be returned -
 *        PRESENT/FAILED
 */
int onlp_voltagei_status_get(onlp_oid_t id, uint32_t* rv);

/**
 * @brief Retrieve the voltage's OID hdr.
 * @param id The voltage OID.
 * @param rv [out] Receives the OID header.
 */
int onlp_voltagei_hdr_get(onlp_oid_t id, onlp_oid_hdr_t* hdr);


#endif /* __ONLP_VOLTAGEI_H__ */

