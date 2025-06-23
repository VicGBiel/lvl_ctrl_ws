#ifndef HC_SR04_H
#define HC_SR04_H

typedef struct{
    int trig_pin;
    int echo_pin;
    float distance_cm;
} HC_SR04_t;

void hc_sr04_init(HC_SR04_t *hc_sr04, int trig_pin, int echo_pin);
void hc_sr04_get_distance(HC_SR04_t *hc_sr04);

#endif