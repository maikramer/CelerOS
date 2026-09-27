/**
 * @file supabase_config.h
 * @brief Configuração do Supabase - CREDENCIAIS REAIS
 * 
 * ⚠️ ATENÇÃO: Este arquivo contém credenciais sensíveis!
 * Este arquivo está no .gitignore e NÃO será commitado.
 * 
 * Para desenvolvimento local, você pode usar este arquivo.
 * Para produção, considere usar NVS para armazenar credenciais.
 */

#ifndef SUPABASE_CONFIG_H
#define SUPABASE_CONFIG_H

// URL do projeto Supabase
#define SUPABASE_URL "https://supabase.locatelli.app.br"

// Anon Key (chave pública - pode ser usada no cliente)
#define SUPABASE_ANON_KEY "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.ewogICJyb2xlIjogImFub24iLAogICJpc3MiOiAic3VwYWJhc2UiLAogICJpYXQiOiAxNzI1OTM3MjAwLAogICJleHAiOiAxODgzNzAzNjAwCn0.0dWerXjYrMltHqSJj6ObDWcL37j9Dicw1_LkYFCQDOI"

// Service Role Key (chave privada - NÃO usar em produção no dispositivo!)
// Mantida aqui apenas para referência, mas NÃO deve ser usada no ESP32
#define SUPABASE_SERVICE_ROLE_KEY "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.ewogICJyb2xlIjogInNlcnZpY2Vfcm9sZSIsCiAgImlzcyI6ICJzdXBhYmFzZSIsCiAgImlhdCI6IDE3MjU5MzcyMDAsCiAgImV4cCI6IDE4ODM3MDM2MDAKfQ.B13xntigEgB9_EYh9xMEyssCeSquNB4lGPtY8smx8vU"

// Nome da tabela para armazenar avaliações
#define SUPABASE_TABLE_NAME "ratings"

#endif // SUPABASE_CONFIG_H

