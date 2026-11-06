/* Cau hinh gia cho cong cu chay lai video tren PC (giong sdkconfig.defaults cua board; HFOV/CAM_H co the doi bang -D) */
#pragma once
#define CONFIG_EVCARHUD_DETECT 1
#ifndef CONFIG_EVCARHUD_ADAS_HFOV_DEG
#define CONFIG_EVCARHUD_ADAS_HFOV_DEG 42
#endif
#define CONFIG_EVCARHUD_ADAS_HORIZON_PCT 45
#define CONFIG_EVCARHUD_ADAS_AUTO_HORIZON 1
#ifndef CONFIG_EVCARHUD_ADAS_CAM_H_CM
#define CONFIG_EVCARHUD_ADAS_CAM_H_CM 130
#endif
