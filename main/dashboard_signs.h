/* Sinh tu gen_all.py: icon canh bao (48x48) va mui ten re (32x32), ARGB8888 */
#ifndef DASHBOARD_SIGNS_H
#define DASHBOARD_SIGNS_H
#include "dashboard.h"
extern const lv_image_dsc_t sign_hazard;
extern const lv_image_dsc_t sign_accident;
extern const lv_image_dsc_t sign_jam;
extern const lv_image_dsc_t sign_railway;
extern const lv_image_dsc_t sign_construction;
extern const lv_image_dsc_t sign_pothole;
extern const lv_image_dsc_t sign_weather;
extern const lv_image_dsc_t sign_pedestrian;
extern const lv_image_dsc_t sign_traffic_light;
extern const lv_image_dsc_t sign_speed_bump;
extern const lv_image_dsc_t sign_curve;
extern const lv_image_dsc_t sign_no_entry;
extern const lv_image_dsc_t sign_no_passing;
extern const lv_image_dsc_t sign_end_no_passing;
extern const lv_image_dsc_t sign_no_left;
extern const lv_image_dsc_t sign_no_right;
extern const lv_image_dsc_t sign_no_uturn;
extern const lv_image_dsc_t sign_no_straight;
extern const lv_image_dsc_t sign_no_car;
extern const lv_image_dsc_t sign_no_moto;
extern const lv_image_dsc_t sign_end_all;
extern const lv_image_dsc_t sign_mand_straight;
extern const lv_image_dsc_t sign_mand_left;
extern const lv_image_dsc_t sign_mand_right;
extern const lv_image_dsc_t sign_one_way;
extern const lv_image_dsc_t sign_camera;
extern const lv_image_dsc_t sign_cam_speed;
extern const lv_image_dsc_t sign_cam_red;
extern const lv_image_dsc_t sign_police;
extern const lv_image_dsc_t sign_toll;
extern const lv_image_dsc_t sign_info;
extern const lv_image_dsc_t sign_residential;
extern const lv_image_dsc_t sign_residential_end;
extern const lv_image_dsc_t arrow_straight;
extern const lv_image_dsc_t arrow_left;
extern const lv_image_dsc_t arrow_right;
extern const lv_image_dsc_t arrow_slight_left;
extern const lv_image_dsc_t arrow_slight_right;
extern const lv_image_dsc_t arrow_sharp_left;
extern const lv_image_dsc_t arrow_sharp_right;
extern const lv_image_dsc_t arrow_uturn;
extern const lv_image_dsc_t arrow_roundabout;
extern const lv_image_dsc_t arrow_arrive;
extern const lv_image_dsc_t wx_clear;
extern const lv_image_dsc_t wx_partly;
extern const lv_image_dsc_t wx_cloudy;
extern const lv_image_dsc_t wx_rain;
extern const lv_image_dsc_t wx_storm;
extern const lv_image_dsc_t wx_fog;

/* Ma canh bao HLP `alr` (0..75) -> icon; ma la -> bien nguy hiem chung. Luon tra ve icon. */
const lv_image_dsc_t *sign_for_alert(int code);
/* Ma huong re `trn` -> mui ten; NULL = khong ve gi. */
const lv_image_dsc_t *arrow_for_turn(int trn);
/* Thoi tiet 0..5 (clear, partly, cloudy, rain, storm, fog) -> icon 40x40; NULL neu ngoai khoang. */
const lv_image_dsc_t *weather_icon(int cond);
/* UI-ICONS BEGIN */
extern const lv_image_dsc_t icon_nav_marker;   /* 48x48: mui ten vi tri (trang, co bong) */
extern const lv_image_dsc_t icon_tri;          /* 12x10: tam giac trang, doi mau bang image_recolor */
extern const lv_image_dsc_t icon_ego_car;      /* 64x51: xe minh (duoi VF7 trang, ve phang) cho trang 0 */
extern const lv_image_dsc_t icon_ego_car_s;    /* 44x35: ban nho cho trang hai the */
/* UI-ICONS END */

#endif
