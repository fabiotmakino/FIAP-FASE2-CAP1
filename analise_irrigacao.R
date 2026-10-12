# =============================================================================
# FarmTech Solutions - Fase 2 | Ir Além 2: análise estatística em R
#
# Entrada : log copiado do Monitor Serial do Wokwi (salve como serial_log.txt).
#           O ESP32 imprime linhas no formato:
#           CSV,tempo_s,n,p,k,ph,umidade,chuva_mm,bomba
# Saída   : resumo estatístico, gráfico (grafico_umidade.png) e a recomendação
#           R=1 (pode irrigar) ou R=0 (não irrigar) para digitar no Monitor Serial.
#
# Métodos : estatística descritiva, correlação, regressão linear (tendência da
#           umidade com intervalo de predição) e teste t (IC 95% do pH médio).
# Uso     : Rscript analise_irrigacao.R   (ou source() no RStudio)
# =============================================================================

# ---------------- Parâmetros (mesmos do ESP32 - soja) ----------------
ARQUIVO_LOG     <- "serial_log.txt"
ARQUIVO_EXEMPLO <- "serial_log_exemplo.txt"
PH_MIN          <- 6.0
PH_MAX          <- 6.8
UMID_LIGA       <- 60      # % abaixo do qual o solo é considerado seco
LIMITE_CHUVA_MM <- 2.0
JANELA          <- 15      # nº de leituras mais recentes usadas na análise
HORIZONTE       <- 5       # prever a umidade daqui a N leituras
NIVEL_CONF      <- 0.95

# ---------------- 1. Leitura e limpeza dos dados ----------------
arquivo <- if (file.exists(ARQUIVO_LOG)) ARQUIVO_LOG else ARQUIVO_EXEMPLO
cat("Lendo dados de:", arquivo, "\n")

linhas <- trimws(readLines(arquivo, warn = FALSE))
linhas <- linhas[startsWith(linhas, "CSV,")]          # ignora texto que não é dado
if (length(linhas) < 5) stop("Poucas leituras CSV no log (mínimo: 5).")

dados <- read.csv(
  text = paste(linhas, collapse = "\n"),
  header = FALSE,
  na.strings = c("nan", "NaN", "NA"),
  col.names = c("tag", "tempo_s", "n", "p", "k", "ph", "umidade", "chuva_mm", "bomba")
)
dados$tag <- NULL
dados <- na.omit(dados)
cat("Leituras válidas:", nrow(dados), "\n\n")

# ---------------- 2. Estatística descritiva e correlação ----------------
cat("=== Resumo estatístico ===\n")
print(summary(dados[, c("ph", "umidade", "chuva_mm")]))

cat("\n=== Correlação (Pearson) ===\n")
cols_cor <- c("ph", "umidade", "chuva_mm", "bomba")
cols_cor <- cols_cor[sapply(dados[cols_cor], function(x) sd(x) > 0)]  # remove colunas constantes
if (length(cols_cor) >= 2) {
  print(round(cor(dados[, cols_cor]), 2))
} else {
  cat("Variação insuficiente nos dados para calcular correlações.\n")
}

# ---------------- 3. Janela recente ----------------
recentes <- tail(dados, JANELA)

# ---------------- 4. Regressão linear: tendência da umidade ----------------
modelo <- lm(umidade ~ tempo_s, data = recentes)
passo  <- median(diff(recentes$tempo_s))
t_fut  <- max(recentes$tempo_s) + HORIZONTE * passo
pred   <- predict(modelo, newdata = data.frame(tempo_s = t_fut),
                  interval = "prediction", level = NIVEL_CONF)

umid_prevista <- pred[1, "fit"]
inclinacao    <- coef(modelo)[["tempo_s"]]
r2            <- summary(modelo)$r.squared

cat("\n=== Regressão linear (últimas", nrow(recentes), "leituras) ===\n")
cat(sprintf("Tendência: %+.3f %% de umidade por segundo (R² = %.2f)\n", inclinacao, r2))
cat(sprintf("Umidade prevista em %d leituras: %.1f %% (IC %.0f%%: %.1f a %.1f)\n",
            HORIZONTE, umid_prevista, NIVEL_CONF * 100, pred[1, "lwr"], pred[1, "upr"]))

# ---------------- 5. Teste t: pH médio ----------------
if (sd(recentes$ph) > 0) {
  ic_ph <- t.test(recentes$ph, conf.level = NIVEL_CONF)$conf.int
} else {
  ic_ph <- c(mean(recentes$ph), mean(recentes$ph))
}
ph_medio <- mean(recentes$ph)
cat(sprintf("\npH médio: %.2f (IC %.0f%%: %.2f a %.2f) | faixa ideal: %.1f a %.1f\n",
            ph_medio, NIVEL_CONF * 100, ic_ph[1], ic_ph[2], PH_MIN, PH_MAX))

# ---------------- 6. Decisão ----------------
ultima    <- tail(dados, 1)
ph_ok     <- ph_medio >= PH_MIN && ph_medio <= PH_MAX
nutri_ok  <- ultima$p == 1 && ultima$k == 1           # soja: P e K obrigatórios
sem_chuva <- ultima$chuva_mm < LIMITE_CHUVA_MM
solo_seco <- umid_prevista < UMID_LIGA

irrigar <- solo_seco && ph_ok && nutri_ok && sem_chuva

cat("\n=== Decisão ===\n")
cat("Solo seco (umidade prevista < ", UMID_LIGA, "%): ", solo_seco, "\n", sep = "")
cat("pH médio na faixa ideal:                  ", ph_ok, "\n", sep = "")
cat("P e K adequados (última leitura):         ", nutri_ok, "\n", sep = "")
cat("Sem previsão de chuva relevante:          ", sem_chuva, "\n", sep = "")
cat("\n>>> Recomendação:", ifelse(irrigar, "IRRIGAR", "NÃO IRRIGAR"), "\n")
cat("\nDigite no Monitor Serial do Wokwi (e dê Enter):\n\n    R=",
    as.integer(irrigar), "\n\n", sep = "")

# ---------------- 7. Gráfico ----------------
png("grafico_umidade.png", width = 900, height = 520)
plot(dados$tempo_s, dados$umidade, type = "b", pch = 19, col = "steelblue",
     xlab = "Tempo (s)", ylab = "Umidade do solo (%)",
     main = "Umidade do solo, tendência e previsão",
     xlim = c(min(dados$tempo_s), t_fut),
     ylim = range(c(dados$umidade, pred[1, c("lwr", "upr")])))
abline(h = UMID_LIGA, col = "red", lty = 2)
tempos <- seq(min(recentes$tempo_s), t_fut, length.out = 50)
lines(tempos, predict(modelo, data.frame(tempo_s = tempos)), col = "darkgreen", lwd = 2)
points(t_fut, umid_prevista, pch = 17, cex = 1.6, col = "darkgreen")
segments(t_fut, pred[1, "lwr"], t_fut, pred[1, "upr"], col = "darkgreen", lwd = 2)
legend("topright", bty = "n",
       legend = c("Leituras", "Limite de irrigação (60%)", "Tendência (janela recente)", "Previsão ± IC"),
       col = c("steelblue", "red", "darkgreen", "darkgreen"),
       lty = c(1, 2, 1, NA), pch = c(19, NA, NA, 17))
dev.off()
cat("Gráfico salvo em grafico_umidade.png\n")
