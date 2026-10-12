# FarmTech Solutions – Fase 2: Irrigação Inteligente (ESP32 + Wokwi)

## Cultura escolhida: Soja
| Parâmetro | Valor adotado | Justificativa |
|---|---|---|
| Nitrogênio (N) | Não exigido | A soja fixa N do ar por simbiose com bactérias (Bradyrhizobium) |
| Fósforo (P) | Obrigatório | Essencial para raízes e formação de vagens |
| Potássio (K) | Obrigatório | Importante para qualidade e resistência da planta |
| pH | 6,0 a 6,8 | Faixa em que há melhor disponibilidade de nutrientes |
| Umidade do solo | Liga < 60 % · Desliga ≥ 70 % | Histerese para evitar liga/desliga constante |

> Valores de referência: ajuste conforme a pesquisa do grupo (ex.: Embrapa).

## Componentes e ligações
| Componente | Função na simulação | Pino ESP32 |
|---|---|---|
| Botão verde N | Sensor de Nitrogênio | GPIO 14 |
| Botão verde P | Sensor de Fósforo | GPIO 27 |
| Botão verde K | Sensor de Potássio | GPIO 26 |
| LDR (módulo) | Sensor de pH (0–14) | GPIO 34 (AO) |
| DHT22 | Umidade do solo | GPIO 15 (SDA) |
| Relé azul | Bomba d'água | GPIO 18 (IN) |

Botões usam `INPUT_PULLUP` (pressionado = LOW). Cada clique **alterna** o nível do nutriente entre ADEQUADO e BAIXO.

## Lógica de decisão
```
nutrientesOk = P e K adequados
phOk         = 6,0 <= pH <= 6,8
semChuva     = chuva prevista < 2,0 mm        (Ir Além 1 - API de clima)
analiseR     = recomendação do R = 1          (Ir Além 2 - padrão: 1)
condicoesOk  = nutrientesOk E phOk E semChuva E analiseR

Bomba DESLIGADA -> liga se: umidade < 60 % E condicoesOk
Bomba LIGADA    -> desliga se: umidade >= 70 % OU !condicoesOk
Sem leitura do DHT22 -> bomba desligada (segurança)
```
Sem usar os opcionais, `chuva = 0` e `R = 1` por padrão, então o sistema funciona só com os sensores.

## Como usar no Wokwi
1. Crie um projeto **ESP32** em wokwi.com.
2. Substitua o conteúdo de `sketch.ino`, `diagram.json` e `libraries.txt` pelos arquivos desta pasta.
3. Rode a simulação e abra o Monitor Serial.
4. Clique nos botões N, P, K e ajuste o LDR (luminosidade) para simular o pH após cada mudança de nutrientes.
5. Altere a umidade no DHT22 e observe o relé.

## Testes sugeridos
| N | P | K | pH | Umidade | Resultado esperado |
|---|---|---|---|---|---|
| – | OK | OK | 6,4 | 45 % | Bomba **liga** |
| – | OK | OK | 6,4 | 72 % | Bomba **desliga** |
| – | OK | BAIXO | 6,4 | 45 % | Bomba permanece desligada |
| – | OK | OK | 5,0 | 45 % | Bomba permanece desligada (pH fora) |

---

# Ir Além

## Opcional 1 – Python + API OpenWeather (`clima_openweather.py`)
1. Crie uma conta em openweathermap.org e gere uma API key (chaves novas podem levar algumas horas para ativar).
2. Defina a chave e rode:
   ```
   export OPENWEATHER_API_KEY="sua_chave"      # Windows: set OPENWEATHER_API_KEY=sua_chave
   python clima_openweather.py --lat <latitude> --lon <longitude>
   ```
3. O script soma a chuva (mm) prevista para as próximas 12 h, considerando só blocos com probabilidade ≥ 50 %, e imprime um comando, por exemplo `CHUVA=5.2`.
4. No Monitor Serial do Wokwi, digite o comando e dê Enter. O ESP32 lê com `Serial.available()` / `Serial.read()`.
5. Se a previsão for ≥ 2,0 mm, a irrigação é suspensa (e o motivo aparece no monitor).

Sem internet ou sem chave: `python clima_openweather.py --demo chuva` (ou `--demo seco`).
Envio automático (opcional, exige `pip install pyserial`): `--porta <porta ou URL serial>`. No plano gratuito do Wokwi isso costuma não ser viável, então a transferência manual acima é o caminho previsto no enunciado.

## Opcional 2 – Análise em R (`analise_irrigacao.R`)
1. Rode a simulação e copie o texto do Monitor Serial para um arquivo `serial_log.txt` (o script usa só as linhas que começam com `CSV,`).
   Sem esse arquivo, o script usa `serial_log_exemplo.txt`.
2. Execute `Rscript analise_irrigacao.R`.
3. O script faz:
   - **Estatística descritiva e correlação** entre pH, umidade, chuva e bomba;
   - **Regressão linear** da umidade nas últimas 15 leituras, prevendo a umidade daqui a 5 leituras com intervalo de predição de 95 %;
   - **Teste t** para o pH médio, com IC de 95 %;
   - **Decisão**: irrigar se a umidade prevista < 60 %, pH médio na faixa, P e K adequados e sem chuva relevante.
4. Ele imprime `R=1` ou `R=0` e salva `grafico_umidade.png`. Digite o comando no Monitor Serial do Wokwi para o ESP32 respeitar a recomendação.

## Fluxo completo
```
OpenWeather -> Python -> "CHUVA=mm" --\
                                       >-> ESP32 (Wokwi) -> relé/bomba
Log CSV do ESP32 -> R -> "R=0|1" -----/
```
