/*
  FarmTech Solutions - Fase 2
  Sistema de irrigação inteligente (ESP32 + Wokwi)

  Cultura escolhida: SOJA
    - N : não é exigido (a soja fixa nitrogênio do ar via bactérias)
    - P : obrigatório
    - K : obrigatório
    - pH ideal: 6,0 a 6,8
    - Irrigar quando a umidade do solo estiver baixa

  Substituições didáticas:
    - Botões verdes N, P, K  -> sensores de nutrientes (cada clique alterna OK / BAIXO)
    - LDR                    -> sensor de pH (mapeado para 0 a 14)
    - DHT22                  -> umidade do solo
    - Relé azul              -> bomba d'água

  IR ALÉM (comandos digitados no Monitor Serial, terminados com Enter):
    CHUVA=<mm>   previsão de chuva (mm) vinda do script Python/OpenWeather
    R=<0|1>      recomendação da análise estatística em R (1 = pode irrigar)
*/

#include <DHT.h>

// ---------- Pinos ----------
#define PIN_BTN_N  14
#define PIN_BTN_P  27
#define PIN_BTN_K  26
#define PIN_LDR    34   // entrada analógica (AO do módulo LDR)
#define PIN_DHT    15
#define PIN_RELE   18

#define DHTTYPE DHT22
DHT dht(PIN_DHT, DHTTYPE);

// ---------- Parâmetros da cultura (soja) ----------
const float PH_MIN           = 6.0;   // pH mínimo aceitável
const float PH_MAX           = 6.8;   // pH máximo aceitável
const float UMID_LIGA        = 60.0;  // abaixo disso o solo está seco -> liga bomba
const float UMID_DESLIGA     = 70.0;  // acima disso o solo está úmido -> desliga bomba
const float LIMITE_CHUVA_MM  = 2.0;   // previsão >= isto suspende a irrigação

// Se no seu Wokwi o pH subir quando você DIMINUIR a luz, troque para false
const bool LDR_INVERTIDO   = true;
// Relé ativo em nível alto (padrão do módulo do Wokwi)
const bool RELE_ATIVO_ALTO = true;

// ---------- Estado dos botões (N, P, K) ----------
const uint8_t PINOS_BTN[3]     = {PIN_BTN_N, PIN_BTN_P, PIN_BTN_K};
const char*   NOMES[3]         = {"N", "P", "K"};
bool          nutriente[3]     = {false, false, false}; // true = nível adequado
bool          ultimaLeitura[3] = {HIGH, HIGH, HIGH};
bool          estavel[3]       = {HIGH, HIGH, HIGH};
unsigned long tempoMudanca[3]  = {0, 0, 0};
const unsigned long DEBOUNCE_MS = 50;

// ---------- Variáveis de controle ----------
float umidade = NAN;
bool  bombaLigada = false;
unsigned long ultimoCiclo = 0;
const unsigned long INTERVALO_MS = 2000; // mínimo do DHT22

// ---------- Dados externos (Ir Além) ----------
float chuvaPrevistaMM = 0.0;  // atualizado via Serial: CHUVA=<mm>
bool  analiseR        = true; // atualizado via Serial: R=<0|1>
String bufferSerial   = "";

// Cada clique no botão alterna o nível do nutriente (OK <-> BAIXO)
void atualizarBotoes() {
  for (int i = 0; i < 3; i++) {
    bool leitura = digitalRead(PINOS_BTN[i]);
    if (leitura != ultimaLeitura[i]) {
      ultimaLeitura[i] = leitura;
      tempoMudanca[i] = millis();
    }
    if ((millis() - tempoMudanca[i]) > DEBOUNCE_MS && leitura != estavel[i]) {
      estavel[i] = leitura;
      if (estavel[i] == LOW) {            // botão foi pressionado
        nutriente[i] = !nutriente[i];
        Serial.printf(">> Botao %s: nivel %s\n", NOMES[i],
                      nutriente[i] ? "ADEQUADO" : "BAIXO");
      }
    }
  }
}

// Interpreta um comando completo recebido pelo Monitor Serial
void processarComando(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  cmd.replace(',', '.');                  // aceita 2,5 ou 2.5
  if (cmd.length() == 0) return;

  if (cmd.startsWith("CHUVA=")) {
    float v = cmd.substring(6).toFloat();
    chuvaPrevistaMM = (v < 0) ? 0 : v;
    Serial.printf(">> Previsao de chuva recebida: %.1f mm\n", chuvaPrevistaMM);
  } else if (cmd.startsWith("R=")) {
    analiseR = (cmd.substring(2).toInt() != 0);
    Serial.printf(">> Analise em R: %s\n", analiseR ? "PODE IRRIGAR" : "NAO IRRIGAR");
  } else {
    Serial.println(">> Comando desconhecido. Use CHUVA=<mm> ou R=<0|1>");
  }
}

// Lê caracteres do Serial sem bloquear o loop
void lerSerial() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      processarComando(bufferSerial);
      bufferSerial = "";
    } else if (bufferSerial.length() < 40) {
      bufferSerial += c;
    }
  }
}

// Converte a leitura do LDR (0-4095) em pH (0-14)
float lerPH() {
  int bruto = analogRead(PIN_LDR);
  float ph = bruto * 14.0 / 4095.0;
  if (LDR_INVERTIDO) ph = 14.0 - ph;
  return ph;
}

void setBomba(bool liga) {
  bombaLigada = liga;
  digitalWrite(PIN_RELE, (liga == RELE_ATIVO_ALTO) ? HIGH : LOW);
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 3; i++) pinMode(PINOS_BTN[i], INPUT_PULLUP);
  pinMode(PIN_RELE, OUTPUT);
  setBomba(false);
  analogReadResolution(12);
  dht.begin();

  Serial.println("=== FarmTech Solutions - Fase 2 | Cultura: SOJA ===");
  Serial.println("Clique em N, P, K para alternar o nivel de cada nutriente.");
  Serial.println("Ajuste o LDR para simular o pH (6,0 a 6,8 = ideal).");
  Serial.println("Comandos: CHUVA=<mm> | R=<0|1>");
}

void loop() {
  atualizarBotoes();
  lerSerial();

  unsigned long agora = millis();
  if (agora - ultimoCiclo < INTERVALO_MS) return;
  ultimoCiclo = agora;

  float u = dht.readHumidity();
  if (!isnan(u)) umidade = u;

  float ph = lerPH();

  // ---------- Lógica de decisão ----------
  bool nutrientesOk = nutriente[1] && nutriente[2];        // P e K (N não é exigido na soja)
  bool phOk         = (ph >= PH_MIN && ph <= PH_MAX);
  bool semChuva     = (chuvaPrevistaMM < LIMITE_CHUVA_MM);  // Ir Além 1: API de clima
  bool condicoesOk  = nutrientesOk && phOk && semChuva && analiseR;

  if (isnan(umidade)) {
    setBomba(false);                                        // segurança: sem leitura, bomba desligada
  } else if (!bombaLigada) {
    if (umidade < UMID_LIGA && condicoesOk) setBomba(true);
  } else {
    // Histerese: desliga ao atingir umidade alvo ou se as condições piorarem
    if (umidade >= UMID_DESLIGA || !condicoesOk) setBomba(false);
  }

  // ---------- Relatório ----------
  Serial.println("--------------------------------");
  Serial.printf("N: %s | P: %s | K: %s\n",
                nutriente[0] ? "OK" : "BAIXO",
                nutriente[1] ? "OK" : "BAIXO",
                nutriente[2] ? "OK" : "BAIXO");
  Serial.printf("pH (LDR): %.2f  [%s]\n", ph, phOk ? "ideal" : "fora da faixa");
  Serial.printf("Umidade solo (DHT22): %.1f %%\n", umidade);
  Serial.printf("Chuva prevista: %.1f mm | Analise R: %s\n",
                chuvaPrevistaMM, analiseR ? "libera" : "bloqueia");
  Serial.printf("BOMBA: %s\n", bombaLigada ? "LIGADA" : "DESLIGADA");

  // Explica por que a irrigação está bloqueada mesmo com solo seco
  if (!isnan(umidade) && umidade < UMID_LIGA && !bombaLigada) {
    Serial.print("Irrigacao bloqueada por:");
    if (!nutrientesOk) Serial.print(" [P/K baixo]");
    if (!phOk)         Serial.print(" [pH fora da faixa]");
    if (!semChuva)     Serial.print(" [previsao de chuva]");
    if (!analiseR)     Serial.print(" [analise R]");
    Serial.println();
  }

  // Linha CSV para a análise em R: tempo_s,n,p,k,ph,umidade,chuva_mm,bomba
  Serial.printf("CSV,%lu,%d,%d,%d,%.2f,%.1f,%.1f,%d\n",
                agora / 1000,
                nutriente[0], nutriente[1], nutriente[2],
                ph, umidade, chuvaPrevistaMM, bombaLigada);
}
