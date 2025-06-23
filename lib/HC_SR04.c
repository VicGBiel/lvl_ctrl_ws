#include "HC_SR04.h"
#include "stdio.h"
#include "pico/stdlib.h"



void hc_sr04_init(HC_SR04_t *hc_sr04, int trig_pin, int echo_pin){
    hc_sr04->trig_pin = trig_pin;
    hc_sr04->echo_pin = echo_pin;

    gpio_init(trig_pin);
    gpio_set_dir(trig_pin, GPIO_OUT);
    gpio_init(echo_pin);
    gpio_set_dir(echo_pin, GPIO_IN);
}


void hc_sr04_get_distance(HC_SR04_t *hc_sr04){
    // Trigger emite pulso de 10us 
    gpio_put(hc_sr04->trig_pin, 0);
    sleep_us(2);
    gpio_put(hc_sr04->trig_pin, 1);
    sleep_us(10);
    gpio_put(hc_sr04->trig_pin, 0);

    // Espera echo ler nível alto
    uint32_t start = time_us_32();
    while(gpio_get(hc_sr04->echo_pin) == 0){
        if(time_us_32() - start > 30000) break;
    }
    uint32_t t0 = time_us_32();

    // Espera echo ir para nivel baixo
    while(gpio_get(hc_sr04->echo_pin) == 1){
        if(time_us_32() - t0 > 30000) break;
    }
    uint32_t t1 = time_us_32();

    // Pega o dt
    uint32_t pulse = t1 - t0;


    // Converte para cm
    if(pulse == 0 || pulse > 30000){
        hc_sr04->distance_cm = 0.0f;
    }
    else{
        hc_sr04->distance_cm = pulse /  58.0f;
    }
}