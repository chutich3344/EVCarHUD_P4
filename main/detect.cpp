#include "sdkconfig.h"
#include "detect.h"

#if CONFIG_EVCARHUD_DETECT

#include <string.h>
#include "dl_detect_base.hpp"
#include "dl_detect_espdet_postprocessor.hpp"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"

#define EVDET_PART "evdet"                 /* ten vung flash chua mo hinh: phai khop partitions.csv va main/CMakeLists.txt */

static const char *TAG = "detect";

/* Mo hinh ESPDet-Pico tu huan luyen. Cach dung (chuan hoa 0..255, letterbox xam 114, 3 tang buoc 8/16/32) lay theo mau cua
 * Espressif (esp-detection / esp-dl) - phai giong luc huan luyen thi khung moi dung cho. */
class EvDet : public dl::detect::DetectImpl {
public:
    EvDet()
    {
        m_model = new dl::Model(EVDET_PART, fbs::MODEL_LOCATION_IN_FLASH_PARTITION);
        m_model->minimize();
        m_image_preprocessor = new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {255, 255, 255});
        m_image_preprocessor->enable_letterbox({114, 114, 114});
        m_postprocessor = new dl::detect::ESPDetPostProcessor(
            m_model, m_image_preprocessor, 0.25, 0.7, DET_BOX_MAX, {{8, 8, 4, 4}, {16, 16, 8, 8}, {32, 32, 16, 16}});
    }

    /* Giong DetectImpl::run() cua ESP-DL, khac 2 diem: chay mo hinh tren ca 2 nhan cua P4 (mac dinh chi 1 nhan),
     * va do rieng thoi gian tung khau de biet cham o dau. */
    std::list<dl::detect::result_t> &run(const dl::image::img_t &img) override
    {
        int64_t t0 = esp_timer_get_time();
        m_image_preprocessor->preprocess(img);
        int64_t t1 = esp_timer_get_time();
        m_model->run(dl::RUNTIME_MODE_MULTI_CORE);
        int64_t t2 = esp_timer_get_time();
        m_postprocessor->clear_result();
        m_postprocessor->postprocess();
        std::list<dl::detect::result_t> &res = m_postprocessor->get_result(img.width, img.height);
        int64_t t3 = esp_timer_get_time();
        m_us[0] += t1 - t0;
        m_us[1] += t2 - t1;
        m_us[2] += t3 - t2;
        if (++m_runs >= 40) {
            ESP_LOGI(TAG, "trung binh %d lan: tien xu ly %d ms, mo hinh %d ms, hau xu ly %d ms", m_runs,
                     (int)(m_us[0] / m_runs / 1000), (int)(m_us[1] / m_runs / 1000), (int)(m_us[2] / m_runs / 1000));
            m_us[0] = m_us[1] = m_us[2] = 0;
            m_runs = 0;
        }
        return res;
    }

private:
    int64_t m_us[3] = {0, 0, 0};
    int m_runs = 0;
};

static EvDet *s_det;
static bool s_bad;                         /* vung flash khong co mo hinh hop le: thoi khong thu nua */

/* ESP-DL dung chuong trinh (abort) neu vung flash trong hay sai dinh dang -> board khoi dong lai lien tuc.
 * Kiem tra truoc: file .espdl don bat dau bang "EDL1" hoac "EDL2" (ban esp-ppq hien nay xuat EDL2). */
static bool model_present(void)
{
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, EVDET_PART);
    char magic[4];
    if (!p) {
        ESP_LOGE(TAG, "khong co vung flash \"%s\" (bang phan vung cu?) - tat nhan dien", EVDET_PART);
        return false;
    }
    if (esp_partition_read(p, 0, magic, sizeof magic) != ESP_OK || (memcmp(magic, "EDL1", 4) != 0 && memcmp(magic, "EDL2", 4) != 0)) {
        ESP_LOGE(TAG, "vung flash \"%s\" chua co mo hinh (nap bang cap: idf.py flash) - tat nhan dien", EVDET_PART);
        return false;
    }
    return true;
}

/* Nap capo / taplo cua chinh xe minh hay bi nhan nham la "o to" (diem cao): khung rat rong va det. Hai dang:
 * (1) nam sat day anh; (2) nam ngang mep nap capo, rong gap >= 6 lan cao - khong xe that nao co ti le do
 * (xe buyt dai chay ngang cung chi ~3,7). Phai giong is_own_hood() trong ml/label_video.py. */
static bool is_own_hood(int x1, int y1, int x2, int y2, int w, int h)
{
    int bw = x2 - x1, bh = y2 - y1;
    if (bw * 100 >= w * 80 && y2 >= h - h / 20 && bh * 100 <= h * 35) return true;
    return bw * 100 >= w * 60 && bw >= 6 * bh;
}

/* Mo hinh doi khi ra HAI khung cung lop long vao nhau cho mot vat (vd mot khung "o to" dung, mot khung "o to" nua om ca
 * xe may ben canh). Buoc loc trung cua ESP-DL khong bo duoc vi hai khung to nho khac nhau nhieu. Khung rong hon lam
 * khoang cach bi tinh gan hon that -> giu khung diem cao hon, bo khung kia. Tra ve so khung con lai. */
static int drop_nested(detect_box_t *b, int n)
{
    bool dead[DET_BOX_MAX] = {};
    if (n > DET_BOX_MAX) n = DET_BOX_MAX;
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (dead[i] || dead[j] || b[i].cls != b[j].cls) continue;
            int iw = (b[i].x2 < b[j].x2 ? b[i].x2 : b[j].x2) - (b[i].x1 > b[j].x1 ? b[i].x1 : b[j].x1);
            int ih = (b[i].y2 < b[j].y2 ? b[i].y2 : b[j].y2) - (b[i].y1 > b[j].y1 ? b[i].y1 : b[j].y1);
            if (iw <= 0 || ih <= 0) continue;
            int ai = (b[i].x2 - b[i].x1) * (b[i].y2 - b[i].y1), aj = (b[j].x2 - b[j].x1) * (b[j].y2 - b[j].y1);
            int amin = ai < aj ? ai : aj;
            if (amin <= 0 || iw * ih * 100 < amin * 85) continue;         /* khung nho phai nam >= 85% trong khung lon */
            if (b[i].score != b[j].score) dead[b[i].score < b[j].score ? i : j] = true;
            else dead[ai > aj ? i : j] = true;                           /* bang diem: bo khung to */
        }
    }
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (!dead[i]) b[k++] = b[i];
    }
    return k;
}

extern "C" int detect_run_rgb565(const uint8_t *rgb565, int w, int h, detect_box_t *out, int max, int *ms)
{
    if (ms) *ms = 0;
    if (s_bad) return -1;
    int64_t t0 = esp_timer_get_time();
    if (!s_det) {
        if (!model_present()) {
            s_bad = true;
            return -1;
        }
        s_det = new EvDet();
        ESP_LOGI(TAG, "da nap mo hinh tu vung flash \"%s\"", EVDET_PART);
    }

    dl::image::img_t img = {};
    img.data = (void *)rgb565;
    img.width = (uint16_t)w;
    img.height = (uint16_t)h;
    /* usb_cam.c cau hinh bo giai ma JPEG phan cung voi thu tu BGR (RGB565 little-endian, dung dinh dang LVGL):
     * ESP-DL goi la RGB565LE (theo dl_image_jpeg.cpp). Chon sai thi anh vao mo hinh bi loan mau, khong nhan ra gi. */
    img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE;

    auto &res = s_det->run(img);
    if (ms) *ms = (int)((esp_timer_get_time() - t0) / 1000);

    int n = 0;
    for (const auto &r : res) {
        if (n >= max) break;
        if (r.category < 0 || r.category >= DET_CLS_N) continue;
        if (is_own_hood(r.box[0], r.box[1], r.box[2], r.box[3], w, h)) continue;
        out[n].cls = r.category;
        out[n].x1 = r.box[0];
        out[n].y1 = r.box[1];
        out[n].x2 = r.box[2];
        out[n].y2 = r.box[3];
        out[n].score = (int)(r.score * 100.0f + 0.5f);
        out[n].dist_m = 0;
        n++;
    }
    return drop_nested(out, n);
}

#else

extern "C" int detect_run_rgb565(const uint8_t *rgb565, int w, int h, detect_box_t *out, int max, int *ms)
{
    (void)rgb565; (void)w; (void)h; (void)out; (void)max;
    if (ms) *ms = 0;
    return -1;
}

#endif
