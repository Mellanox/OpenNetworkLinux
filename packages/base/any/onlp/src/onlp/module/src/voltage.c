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
#include <onlp/voltage.h>
#include <onlp/platformi/voltagei.h>
#include <onlp/oids.h>
#include "onlp_int.h"
#include "onlp_locks.h"

#define VALIDATE(_id)                           \
    do {                                        \
        if(!ONLP_OID_IS_VOLTAGE(_id)) {         \
            return ONLP_STATUS_E_INVALID;       \
        }                                       \
    } while(0)

#define VALIDATENR(_id)                         \
    do {                                        \
        if(!ONLP_OID_IS_VOLTAGE(_id)) {         \
            return;                             \
        }                                       \
    } while(0)


static int
onlp_voltage_init_locked__(void)
{
    return onlp_voltagei_init();
}
ONLP_LOCKED_API0(onlp_voltage_init);



static int
onlp_voltage_info_get_locked__(onlp_oid_t oid, onlp_voltage_info_t* info)
{
    int rv;
    VALIDATE(oid);

    rv = onlp_voltagei_info_get(oid, info);
    return rv;
}
ONLP_LOCKED_API2(onlp_voltage_info_get, onlp_oid_t, oid, onlp_voltage_info_t*, info);

static int
onlp_voltage_status_get_locked__(onlp_oid_t id, uint32_t* status)
{
    int rv = onlp_voltagei_status_get(id, status);
    if(ONLP_SUCCESS(rv)) {
        return rv;
    }
    if(ONLP_UNSUPPORTED(rv)) {
        onlp_voltage_info_t ti;
        rv = onlp_voltagei_info_get(id, &ti);
        *status = ti.status;
    }
    return rv;
}
ONLP_LOCKED_API2(onlp_voltage_status_get, onlp_oid_t, id, uint32_t*, status);

static int
onlp_voltage_hdr_get_locked__(onlp_oid_t id, onlp_oid_hdr_t* hdr)
{
    int rv = onlp_voltagei_hdr_get(id, hdr);
    if(ONLP_SUCCESS(rv)) {
        return rv;
    }
    if(ONLP_UNSUPPORTED(rv)) {
        onlp_voltage_info_t ti;
        rv = onlp_voltagei_info_get(id, &ti);
        memcpy(hdr, &ti.hdr, sizeof(ti.hdr));
    }
    return rv;
}
ONLP_LOCKED_API2(onlp_voltage_hdr_get, onlp_oid_t, id, onlp_oid_hdr_t*, hdr);


/************************************************************
 *
 * Debug and Show Functions
 *
 ***********************************************************/
void
onlp_voltage_dump(onlp_oid_t id, aim_pvs_t* pvs, uint32_t flags)
{
    int rv;
    iof_t iof;
    onlp_voltage_info_t info;

    VALIDATENR(id);
    onlp_oid_dump_iof_init_default(&iof, pvs);

    iof_push(&iof, "voltage @ %d", ONLP_OID_ID_GET(id));
    rv = onlp_voltage_info_get(id, &info);
    if(rv < 0) {
        onlp_oid_info_get_error(&iof, rv);
    }
    else {
        onlp_oid_show_description(&iof, &info.hdr);
        if(info.status & 1) {
            /* Present */
            iof_iprintf(&iof, "Status: %{onlp_voltage_status_flags}", info.status);
            iof_iprintf(&iof, "Caps:   %{onlp_voltage_caps_flags}", info.caps);
            iof_iprintf(&iof, "Voltage: %d", info.voltage);
            iof_push(&iof, "thresholds");
            {
                iof_iprintf(&iof, "Minimum: %d", info.range.min);
                iof_iprintf(&iof, "Maximum: %d", info.range.max);
                iof_pop(&iof);
            }
        }
        else {
            iof_iprintf(&iof, "Not present.");
        }
    }
    iof_pop(&iof);
}

void
onlp_voltage_show(onlp_oid_t id, aim_pvs_t* pvs, uint32_t flags)
{
    int rv;
    iof_t iof;
    onlp_voltage_info_t ti;
    VALIDATENR(id);
    int yaml;

    onlp_oid_show_iof_init_default(&iof, pvs, flags);


    rv = onlp_voltage_info_get(id, &ti);

    yaml = flags & ONLP_OID_SHOW_F_YAML;

    if(yaml) {
        iof_push(&iof, "- ");
        iof_iprintf(&iof, "Name: Voltage %d", ONLP_OID_ID_GET(id));
    }
    else {
        iof_push(&iof, "Voltage %d", ONLP_OID_ID_GET(id));
    }

    if(rv < 0) {
        if(yaml) {
            iof_iprintf(&iof, "State: Error");
            iof_iprintf(&iof, "Error: %{onlp_status}", rv);
        }
        else {
            onlp_oid_info_get_error(&iof, rv);
        }
    }
    else {
        onlp_oid_show_description(&iof, &ti.hdr);
        if(ti.status & 0x1) {
            /* Present */
            if(ti.status & ONLP_VOLTAGE_STATUS_FAILED) {
                iof_iprintf(&iof, "Status: Failed");
            }
            else {
                iof_iprintf(&iof, "Status: Functional");
                iof_iprintf(&iof, "Voltage: %d.%d C", ti.voltage);
            }
        }
        else {
            /* Not present */
            onlp_oid_show_state_missing(&iof);
        }
    }
    iof_pop(&iof);
}
