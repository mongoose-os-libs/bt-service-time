/*
 * Copyright (c) 2014-2018 Cesanta Software Limited
 * All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the ""License"");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an ""AS IS"" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "esp_gatt_defs.h"

#include "common/cs_dbg.h"
#include "common/cs_time.h"

#include "mgos_event.h"
#include "mgos_sys_config.h"

#include "esp32_bt.h"
#include "esp32_bt_gatts.h"

/* Note: partial implementation, no notifications or time reference info. */

static const uint16_t time_svc_uuid = ESP_GATT_UUID_CURRENT_TIME_SVC;
static const uint16_t current_time_uuid = ESP_GATT_UUID_CURRENT_TIME;
static uint16_t current_time_ah;

const esp_gatts_attr_db_t time_svc_gatt_db[3] = {
    {
     .attr_control = {.auto_rsp = ESP_GATT_AUTO_RSP},
     .att_desc =
         {
          .uuid_length = ESP_UUID_LEN_16,
          .uuid_p = (uint8_t *) &primary_service_uuid,
          .perm = ESP_GATT_PERM_READ,
          .max_length = ESP_UUID_LEN_128,
          .length = ESP_UUID_LEN_16,
          .value = (uint8_t *) &time_svc_uuid,
         },
    },
    /* current_time */
    {{ESP_GATT_AUTO_RSP},
     {ESP_UUID_LEN_16, (uint8_t *) &char_decl_uuid, ESP_GATT_PERM_READ, 1, 1,
      (uint8_t *) &char_prop_read}},
    {{ESP_GATT_RSP_BY_APP},
     {ESP_UUID_LEN_16, (uint8_t *) &current_time_uuid, ESP_GATT_PERM_READ, 0, 0,
      NULL}},
};

/* https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.characteristic.current_time.xml
 */
struct bt_date_time {
  uint16_t year;
  uint8_t mon;
  uint8_t mday;
  uint8_t hour;
  uint8_t min;
  uint8_t sec;
} __attribute__((packed));

struct bt_day_date_time {
  struct bt_date_time date_time;
  uint8_t dow;
} __attribute__((packed));

struct bt_exact_time_256 {
  struct bt_day_date_time day_date_time;
  uint8_t s256;
} __attribute__((packed));

struct bt_cur_time_resp {
  struct bt_exact_time_256 exact_time_256;
  uint8_t adj_reason;
} __attribute__((packed));

static bool time_svc_ev(struct esp32_bt_session *bs, esp_gatts_cb_event_t ev,
                        esp_ble_gatts_cb_param_t *ep) {
  bool ret = false;
  switch (ev) {
    case ESP_GATTS_CREAT_ATTR_TAB_EVT: {
      const struct gatts_add_attr_tab_evt_param *p = &ep->add_attr_tab;
      current_time_ah = p->handles[2];
      break;
    }
    case ESP_GATTS_READ_EVT: {
      struct bt_cur_time_resp resp;
      const struct gatts_read_evt_param *p = &ep->read;
      if (p->handle != current_time_ah || p->offset != 0) {
        break;
      }
      memset(&resp, 0, sizeof(resp));
      const double now = cs_time();
      const time_t time = (time_t) now;
      struct tm tm;
      if (gmtime_r(&time, &tm) == NULL) {
        break;
      }
      resp.exact_time_256.day_date_time.date_time.year = tm.tm_year + 1900;
      resp.exact_time_256.day_date_time.date_time.mon = tm.tm_mon + 1;
      resp.exact_time_256.day_date_time.date_time.mday = tm.tm_mday + 1;
      resp.exact_time_256.day_date_time.date_time.hour = tm.tm_hour;
      resp.exact_time_256.day_date_time.date_time.min = tm.tm_min;
      resp.exact_time_256.day_date_time.date_time.sec = tm.tm_sec;
      resp.exact_time_256.day_date_time.dow =
          (tm.tm_wday == 0 ? 7 : tm.tm_wday);
      resp.exact_time_256.s256 = (uint8_t)((now - time) / (1.0 / 256));
      resp.adj_reason = 0;
      esp_gatt_rsp_t rsp = {.attr_value = {.handle = p->handle,
                                           .offset = p->offset,
                                           .len = sizeof(resp)}};
      memcpy(rsp.attr_value.value, &resp, sizeof(resp));
      esp_ble_gatts_send_response(bs->bc->gatt_if, bs->bc->conn_id, p->trans_id,
                                  ESP_GATT_OK, &rsp);
      ret = true;
      break;
    }
    default:
      break;
  }
  return ret;
}

bool mgos_bt_service_time_init(void) {
  if (mgos_sys_config_get_bt_time_svc_enable()) {
    mgos_bt_gatts_register_service(time_svc_gatt_db,
                                   ARRAY_SIZE(time_svc_gatt_db), time_svc_ev);
  }
  return true;
}
