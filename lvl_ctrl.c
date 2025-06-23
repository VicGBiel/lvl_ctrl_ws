#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/tcp.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ssd1306.h"
#include "font.h"
#include "pico/bootrom.h"
#include "HC_SR04.h"

// Pino da bomba (reutilizando o pino do LED original)
#define BOMBA_PIN 12 
#define BOTAO_A 5
#define BOTAO_B 6
#define BOTAO_JOY 22
#define JOYSTICK_X 26
#define JOYSTICK_Y 27

#define WIFI_SSID "bythesword [2.4GHz]"
#define WIFI_PASS "30317512"

#define I2C_PORT_DISP i2c1
#define I2C_SDA_DISP 14
#define I2C_SCL_DISP 15
#define endereco 0x3C

#define TRIG_PIN 17
#define ECHO_PIN 16

// --- Variáveis Globais para Controle de Nível ---
float nivel_minimo = 20.0;  // Nível baixo (em cm). Se a distância for MAIOR que isso, a bomba liga.
float nivel_maximo = 5.0;   // Nível alto (em cm). Se a distância for MENOR que isso, a bomba desliga.
bool bomba_ligada = false;

// --- HTML Modificado ---
const char HTML_BODY[] =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Controle de Nivel do Reservatorio</title>"
    "<style>"
    "body { font-family: sans-serif; text-align: center; padding: 20px; margin: 0; background: #f0f7ff; color: #333; }"
    "h1 { color: #0056b3; }"
    ".container { max-width: 500px; margin: auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); }"
    ".status { font-size: 22px; margin: 20px 0; }"
    ".status-label { font-weight: bold; color: #555; }"
    ".status-value { padding: 5px 10px; border-radius: 5px; color: white; }"
    "#bomba_status.ligada { background-color: #28a745; }"
    "#bomba_status.desligada { background-color: #dc3545; }"
    "form { margin-top: 30px; display: flex; flex-direction: column; gap: 15px; }"
    "label { font-weight: bold; text-align: left; }"
    "input[type='number'] { padding: 10px; border: 1px solid #ccc; border-radius: 5px; font-size: 16px; }"
    "button { font-size: 18px; padding: 12px 25px; border: none; border-radius: 8px; background: #007bff; color: white; cursor: pointer; transition: background-color 0.3s; }"
    "button:hover { background: #0056b3; }"
    "</style>"
    "<script>"
    "function atualizar() {"
    "  fetch('/estado').then(res => res.json()).then(data => {"
    "    document.getElementById('nivel_atual').innerText = data.nivel_atual.toFixed(2) + ' cm';"
    "    const bombaStatusEl = document.getElementById('bomba_status');"
    "    bombaStatusEl.innerText = data.bomba_status ? 'Ligada' : 'Desligada';"
    "    bombaStatusEl.className = 'status-value ' + (data.bomba_status ? 'ligada' : 'desligada');"
    "    if (document.activeElement.type !== 'number') {" // Não atualiza os inputs se o usuário estiver digitando
    "      document.getElementById('min').value = data.nivel_minimo;"
    "      document.getElementById('max').value = data.nivel_maximo;"
    "    }"
    "  });"
    "}"
    "function salvarConfig(event) {"
    "  event.preventDefault();"
    "  const min_val = document.getElementById('min').value;"
    "  const max_val = document.getElementById('max').value;"
    "  fetch('/config', {"
    "    method: 'POST',"
    "    headers: {'Content-Type': 'application/x-www-form-urlencoded'},"
    "    body: 'min=' + min_val + '&max=' + max_val"
    "  }).then(() => alert('Configuracao salva!'));"
    "}"
    "setInterval(atualizar, 2000);"
    "window.onload = atualizar;"
    "</script></head><body>"
    "<div class='container'>"
    "<h1>Controle de Nivel do Reservatorio</h1>"
    "<div class='status'><span class='status-label'>Nivel Atual: </span><span id='nivel_atual'>--</span></div>"
    "<div class='status'><span class='status-label'>Status da Bomba: </span><span id='bomba_status' class='status-value desligada'>--</span></div>"
    "<hr>"
    "<form onsubmit='salvarConfig(event)'>"
    "<h2>Configuracao</h2>"
    "<label for='max'>Nivel Maximo (Distancia em cm do sensor):</label>"
    "<input type='number' id='max' name='max' step='0.1' required>"
    "<label for='min'>Nivel Minimo (Distancia em cm do sensor):</label>"
    "<input type='number' id='min' name='min' step='0.1' required>"
    "<button type='submit'>Salvar</button>"
    "</form>"
    "</div>"
    "</body></html>";

struct http_state
{
    char response[4096];
    size_t len;
    size_t sent;
};

void setup_gpio();
static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);
static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err);
static void start_http_server(void);
void gpio_irq_handler(uint gpio, uint32_t events);

int main(){

    setup_gpio();
    stdio_init_all();

    gpio_set_irq_enabled_with_callback(BOTAO_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    sleep_ms(1000);

    adc_init();

    i2c_init(I2C_PORT_DISP, 400 * 1000);
    gpio_set_function(I2C_SDA_DISP, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_DISP, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_DISP);
    gpio_pull_up(I2C_SCL_DISP);

    ssd1306_t ssd;
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT_DISP);
    ssd1306_config(&ssd);
    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "Iniciando Wi-Fi", 0, 0);
    ssd1306_draw_string(&ssd, "Aguarde...", 0, 30);    
    ssd1306_send_data(&ssd);

    // Iniciando sensor utlrassonico
    HC_SR04_t hc_sr04; 
    hc_sr04_init(&hc_sr04, TRIG_PIN, ECHO_PIN);

    if (cyw43_arch_init())
    {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "WiFi => FALHA", 0, 0);
        ssd1306_send_data(&ssd);
        return 1;
    }

    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 10000))
    {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "WiFi => ERRO", 0, 0);
        ssd1306_send_data(&ssd);
        return 1;
    }

    uint8_t *ip = (uint8_t *)&(cyw43_state.netif[0].ip_addr.addr);
    char ip_str[24];
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "WiFi => OK", 0, 0);
    ssd1306_draw_string(&ssd, ip_str, 0, 10);
    ssd1306_send_data(&ssd);

    start_http_server();
    char str_x[5]; // Buffer para armazenar a string
    char str_y[5]; // Buffer para armazenar a string
    bool cor = true;

    while (true){
        cyw43_arch_poll();

        hc_sr04_get_distance(&hc_sr04);
        printf("%.2f\n", hc_sr04.distance_cm);


        ssd1306_draw_string(&ssd, "CEPEDI   TIC37", 8, 6); // Desenha uma string
        ssd1306_draw_string(&ssd, "EMBARCATECH", 20, 16);  // Desenha uma string
        ssd1306_draw_string(&ssd, ip_str, 10, 28);
        ssd1306_draw_string(&ssd, "X    Y    PB", 20, 41);           // Desenha uma string
        ssd1306_line(&ssd, 44, 37, 44, 60, cor);                     // Desenha uma linha vertical
        ssd1306_draw_string(&ssd, str_x, 8, 52);                     // Desenha uma string
        ssd1306_line(&ssd, 84, 37, 84, 60, cor);                     // Desenha uma linha vertical
        ssd1306_draw_string(&ssd, str_y, 49, 52);                    // Desenha uma string
        ssd1306_rect(&ssd, 52, 90, 8, 8, cor, !gpio_get(BOTAO_JOY)); // Desenha um retângulo
        ssd1306_rect(&ssd, 52, 102, 8, 8, cor, !gpio_get(BOTAO_A));  // Desenha um retângulo
        ssd1306_rect(&ssd, 52, 114, 8, 8, cor, !cor);                // Desenha um retângulo
        ssd1306_send_data(&ssd);                                     // Atualiza o display

        sleep_ms(300);
    }

    cyw43_arch_deinit();
    return 0;
}

void setup_gpio(){
    gpio_init(BOTAO_B);
    gpio_set_dir(BOTAO_B, GPIO_IN);
    gpio_pull_up(BOTAO_B);

    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A);

    gpio_init(BOTAO_JOY);
    gpio_set_dir(BOTAO_JOY, GPIO_IN);
    gpio_pull_up(BOTAO_JOY);
}

static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len){
    struct http_state *hs = (struct http_state *)arg;
    hs->sent += len;
    if (hs->sent >= hs->len)
    {
        tcp_close(tpcb);
        free(hs);
    }
    return ERR_OK;
}

static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        return ERR_OK;
    }

    char *req = (char *)p->payload;
    struct http_state *hs = malloc(sizeof(struct http_state));
    if (!hs)
    {
        pbuf_free(p);
        tcp_close(tpcb);
        return ERR_MEM;
    }
    hs->sent = 0;

    // --- Endpoint para receber a configuração de níveis (MÍNIMO e MÁXIMO) ---
    if (strstr(req, "POST /config")) {
        char *body = strstr(req, "\r\n\r\n");
        if (body) {
            body += 4; // Pula os caracteres \r\n\r\n para chegar ao corpo
            float new_min, new_max;
            if (sscanf(body, "min=%f&max=%f", &new_min, &new_max) == 2) {
                nivel_minimo = new_min;
                nivel_maximo = new_max;
                printf("Novos valores recebidos: MIN=%.2f, MAX=%.2f\n", nivel_minimo, nivel_maximo);
            }
        }
        // Responde com um simples 200 OK
        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Connection: close\r\n\r\n");
    }
    // --- Endpoint para enviar o estado atual (NÍVEL e BOMBA) ---
    else if (strstr(req, "GET /estado"))
    {
        extern float hc_sr04_distance_cm; // Usa a distância já medida no loop principal
        
        char json_payload[128];
        int json_len = snprintf(json_payload, sizeof(json_payload),
                                "{\"nivel_atual\":%.2f,\"bomba_status\":%d,\"nivel_minimo\":%.2f,\"nivel_maximo\":%.2f}\r\n",
                                hc_sr04_distance_cm, bomba_ligada, nivel_minimo, nivel_maximo);

        printf("[DEBUG] JSON: %s\n", json_payload);

        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           json_len, json_payload);
    }
    // --- Serve a página HTML principal ---
    else
    {
        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/html\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           (int)strlen(HTML_BODY), HTML_BODY);
    }

    tcp_arg(tpcb, hs);
    tcp_sent(tpcb, http_sent);

    tcp_write(tpcb, hs->response, hs->len, TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);

    pbuf_free(p);
    return ERR_OK;
}

static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}

static void start_http_server(void)
{
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb)
    {
        printf("Erro ao criar PCB TCP\n");
        return;
    }
    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK)
    {
        printf("Erro ao ligar o servidor na porta 80\n");
        return;
    }
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, connection_callback);
    printf("Servidor HTTP rodando na porta 80...\n");
}

#include "pico/bootrom.h"
#define BOTAO_B 6
void gpio_irq_handler(uint gpio, uint32_t events)
{
    reset_usb_boot(0, 0);
}

float hc_sr04_distance_cm = 0.0; // Variável global para a distância

int main()
{
    gpio_init(BOTAO_B);
    gpio_set_dir(BOTAO_B, GPIO_IN);
    gpio_pull_up(BOTAO_B);
    gpio_set_irq_enabled_with_callback(BOTAO_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    stdio_init_all();
    sleep_ms(2000);

    // Configura o pino da bomba como saída
    gpio_init(BOMBA_PIN);
    gpio_set_dir(BOMBA_PIN, GPIO_OUT);

    // --- O restante das inicializações (botões, ADC, I2C) permanecem ---
    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A);

    gpio_init(BOTAO_JOY);
    gpio_set_dir(BOTAO_JOY, GPIO_IN);
    gpio_pull_up(BOTAO_JOY);

    adc_init();
    adc_gpio_init(JOYSTICK_X);
    adc_gpio_init(JOYSTICK_Y);

    i2c_init(I2C_PORT_DISP, 400 * 1000);
    gpio_set_function(I2C_SDA_DISP, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_DISP, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_DISP);
    gpio_pull_up(I2C_SCL_DISP);

    ssd1306_t ssd;
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT_DISP);
    ssd1306_config(&ssd);
    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "Iniciando Wi-Fi", 0, 0);
    ssd1306_draw_string(&ssd, "Aguarde...", 0, 30);    
    ssd1306_send_data(&ssd);

    // Iniciando sensor ultrassônico
    HC_SR04_t hc_sr04; 
    hc_sr04_init(&hc_sr04, TRIG_PIN, ECHO_PIN);

    if (cyw43_arch_init())
    {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "WiFi => FALHA", 0, 0);
        ssd1306_send_data(&ssd);
        return 1;
    }

    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 10000))
    {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "WiFi => ERRO", 0, 0);
        ssd1306_send_data(&ssd);
        return 1;
    }

    uint8_t *ip = (uint8_t *)&(cyw43_state.netif[0].ip_addr.addr);
    char ip_str[24];
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "WiFi => OK", 0, 0);
    ssd1306_draw_string(&ssd, ip_str, 0, 10);
    ssd1306_send_data(&ssd);

    start_http_server();

    while (true){
        cyw43_arch_poll();

        // Mede a distância
        hc_sr04_get_distance(&hc_sr04);
        hc_sr04_distance_cm = hc_sr04.distance_cm;
        printf("Distancia: %.2f cm\n", hc_sr04_distance_cm);

        // --- Lógica de Controle da Bomba ---
        // Se a distância medida for maior que o nível mínimo (água baixa), liga a bomba.
        if (hc_sr04_distance_cm > nivel_minimo) {
            bomba_ligada = true;
        } 
        // Se a distância medida for menor que o nível máximo (água alta), desliga a bomba.
        else if (hc_sr04_distance_cm < nivel_maximo) {
            bomba_ligada = false;
        }
        
        // Atualiza o estado do pino da bomba
        gpio_put(BOMBA_PIN, bomba_ligada);

        // O código do display OLED foi comentado para focar na funcionalidade web,
        // mas pode ser reativado se necessário.
        
        sleep_ms(500);
    }

    cyw43_arch_deinit();
    return 0;
}