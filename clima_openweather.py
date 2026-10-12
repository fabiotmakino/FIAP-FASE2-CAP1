#!/usr/bin/env python3
"""
FarmTech Solutions - Fase 2 | Ir Além 1: integração com API pública (OpenWeather)

O script consulta a previsão do tempo (API "5 day / 3 hour forecast"), soma a chuva
prevista para as próximas horas e gera o comando que deve ser digitado no
Monitor Serial do ESP32 (Wokwi):

    CHUVA=<mm>

Só usa a biblioteca padrão do Python (nenhuma instalação necessária).
Para envio automático por porta serial, instale opcionalmente: pip install pyserial

Exemplos:
    export OPENWEATHER_API_KEY="sua_chave"          # Windows: set OPENWEATHER_API_KEY=sua_chave
    python clima_openweather.py --lat -23.55 --lon -46.63
    python clima_openweather.py --horas 12 --limite-pop 0.5
    python clima_openweather.py --demo chuva         # sem internet / sem chave (dados simulados)
    python clima_openweather.py --demo seco
    python clima_openweather.py --porta rfc2217://localhost:4000   # envio automático (opcional)
"""

import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime

URL_PREVISAO = "https://api.openweathermap.org/data/2.5/forecast"

# Localização da lavoura (altere para a sua)
LAT_PADRAO = -23.55
LON_PADRAO = -46.63


def buscar_previsao(lat, lon, chave):
    """Consulta a OpenWeather e devolve o JSON da previsão (blocos de 3 horas)."""
    params = urllib.parse.urlencode({
        "lat": lat, "lon": lon, "appid": chave,
        "units": "metric", "lang": "pt_br",
    })
    with urllib.request.urlopen(f"{URL_PREVISAO}?{params}", timeout=15) as resp:
        return json.load(resp)


def previsao_demo(cenario):
    """Gera dados no mesmo formato da API para testes sem internet."""
    agora = int(time.time())
    blocos = []
    for i in range(8):
        bloco = {
            "dt": agora + 3 * 3600 * (i + 1),
            "main": {"temp": 24.0 - i * 0.5},
            "weather": [{"description": "céu limpo"}],
            "pop": 0.05,
        }
        if cenario == "chuva" and i in (1, 2):
            bloco["pop"] = 0.85
            bloco["rain"] = {"3h": 3.4 if i == 1 else 1.8}
            bloco["weather"] = [{"description": "chuva moderada"}]
        blocos.append(bloco)
    return {"city": {"name": "Cenário simulado"}, "list": blocos}


def analisar(dados, horas, limite_pop):
    """
    Soma a chuva (mm) dos blocos de 3h dentro da janela cujo POP (probabilidade
    de precipitação) seja >= limite_pop. Isso evita contar chuva improvável.
    """
    n_blocos = max(1, horas // 3)
    janela = dados["list"][:n_blocos]
    total_mm, pop_max, detalhes = 0.0, 0.0, []

    for b in janela:
        pop = float(b.get("pop", 0.0))
        mm = float(b.get("rain", {}).get("3h", 0.0))
        pop_max = max(pop_max, pop)
        considerado = pop >= limite_pop
        if considerado:
            total_mm += mm
        detalhes.append({
            "hora": datetime.fromtimestamp(b["dt"]).strftime("%d/%m %H:%M"),
            "descricao": b["weather"][0]["description"],
            "temp": b["main"]["temp"],
            "pop": pop, "mm": mm, "considerado": considerado,
        })
    return total_mm, pop_max, detalhes


def enviar_serial(porta, comando):
    """Envio automático opcional (ex.: Wokwi + RFC2217, ou ESP32 real em COMx)."""
    try:
        import serial  # pyserial
    except ImportError:
        sys.exit("pyserial não instalado. Rode: pip install pyserial")
    with serial.serial_for_url(porta, baudrate=115200, timeout=2) as s:
        time.sleep(1)
        s.write((comando + "\n").encode())
        print(f"Comando enviado para {porta}: {comando}")


def main():
    ap = argparse.ArgumentParser(description="Previsão de chuva (OpenWeather) para o ESP32")
    ap.add_argument("--lat", type=float, default=LAT_PADRAO)
    ap.add_argument("--lon", type=float, default=LON_PADRAO)
    ap.add_argument("--chave", default=os.environ.get("OPENWEATHER_API_KEY"),
                    help="API key (ou variável de ambiente OPENWEATHER_API_KEY)")
    ap.add_argument("--horas", type=int, default=12, help="janela de previsão (múltiplo de 3)")
    ap.add_argument("--limite-pop", type=float, default=0.5,
                    help="probabilidade mínima (0 a 1) para considerar a chuva")
    ap.add_argument("--demo", choices=["chuva", "seco"],
                    help="usa dados simulados (sem internet nem chave)")
    ap.add_argument("--porta", help="porta serial/URL para enviar o comando automaticamente")
    args = ap.parse_args()

    if args.demo:
        dados = previsao_demo(args.demo)
    else:
        if not args.chave:
            sys.exit("Informe a API key com --chave ou OPENWEATHER_API_KEY (ou use --demo).")
        try:
            dados = buscar_previsao(args.lat, args.lon, args.chave)
        except urllib.error.HTTPError as e:
            sys.exit(f"Erro HTTP {e.code} ao consultar a API (confira a chave; chaves novas "
                     f"podem levar algumas horas para ativar).")
        except urllib.error.URLError as e:
            sys.exit(f"Falha de conexão: {e.reason}")

    total_mm, pop_max, detalhes = analisar(dados, args.horas, args.limite_pop)

    print(f"\nLocal: {dados.get('city', {}).get('name', '?')} | janela: próximas {args.horas} h")
    print(f"{'Horário':<13}{'Condição':<20}{'Temp':>7}{'POP':>7}{'Chuva':>9}  Conta?")
    for d in detalhes:
        print(f"{d['hora']:<13}{d['descricao']:<20}{d['temp']:>6.1f}°{d['pop']*100:>6.0f}%"
              f"{d['mm']:>7.1f}mm  {'sim' if d['considerado'] else 'não'}")

    decisao = "SUSPENDER irrigação" if total_mm >= 2.0 else "irrigação liberada"
    print(f"\nChuva esperada: {total_mm:.1f} mm (prob. máx. {pop_max*100:.0f}%) -> {decisao} "
          f"(limite no ESP32: 2,0 mm)")

    comando = f"CHUVA={total_mm:.1f}"
    print(f"\nDigite no Monitor Serial do Wokwi (e dê Enter):\n\n    {comando}\n")

    if args.porta:
        enviar_serial(args.porta, comando)


if __name__ == "__main__":
    main()
