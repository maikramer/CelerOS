# Supabase Client para ESP32

Cliente completo para integração com Supabase no ESP32, incluindo operações de banco de dados (PostgREST) e autenticação.

## Índice

- [Características](#características)
- [Configuração](#configuração)
- [SupabaseClient](#supabaseclient)
- [SupabaseAuth](#supabaseauth)
- [Query Builder](#query-builder)
- [Autenticação de Usuário](#autenticação-de-usuário)
- [Row Level Security (RLS)](#row-level-security-rls)
- [Exemplos](#exemplos)

---

## Características

- **PostgREST**: CRUD completo (INSERT, SELECT, UPDATE, DELETE, UPSERT)
- **Query Builder**: Interface fluente para construção de queries
- **RPC**: Chamada de funções do banco de dados
- **Autenticação**: Login, signup, refresh de tokens
- **RLS Support**: Operações autenticadas respeitam Row Level Security
- **Persistência**: Credenciais salvas em NVS
- **TLS**: Suporte a certificados HTTPS

---

## Configuração

### Inicialização Básica

```cpp
#include "SupabaseClient.h"

using namespace Supabase;

void setup() {
    auto& supabase = SupabaseClient::instance();
    
    // Inicializar com URL e API key
    supabase.init(
        "https://xxxxx.supabase.co",
        "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."
    );
    
    // Testar conexão
    if (supabase.testConnection()) {
        ESP_LOGI("App", "Conectado ao Supabase!");
    }
}
```

### Persistência de Credenciais

```cpp
// Salvar credenciais no NVS
supabase.saveCredentials();

// Carregar credenciais do NVS (em próximos boots)
if (supabase.loadCredentials() == CommonErrorCodes::None) {
    ESP_LOGI("App", "Credenciais carregadas");
}
```

### Configuração de Timeout e TLS

```cpp
// Definir timeout
supabase.setTimeout(15000);  // 15 segundos

// Certificado TLS (opcional, para verificação)
extern const uint8_t cert_start[] asm("_binary_supabase_pem_start");
extern const uint8_t cert_end[] asm("_binary_supabase_pem_end");
supabase.setCertificate(cert_start, cert_end);
```

---

## SupabaseClient

### INSERT

```cpp
auto& db = SupabaseClient::instance();

// Insert simples
auto resp = db.from("devices")
    .insert(R"({"name": "Sensor-01", "type": "temperature"})");

if (resp.isOk()) {
    ESP_LOGI("DB", "Inserido com sucesso");
}

// Insert com retorno dos dados
auto resp = db.from("devices")
    .insert(R"({"name": "Sensor-01"})", ReturnPreference::Representation);

if (resp.isOk()) {
    ESP_LOGI("DB", "Dados: %s", resp.body.c_str());
}
```

### SELECT

```cpp
// Select simples
auto resp = db.from("devices")
    .select("*")
    .execute();

// Select com filtros
auto resp = db.from("devices")
    .select("id,name,created_at")
    .eq("type", "temperature")
    .gt("created_at", "2024-01-01")
    .order("name", true)  // ASC
    .limit(10)
    .execute();

// Select único (retorna objeto ao invés de array)
auto resp = db.from("devices")
    .select("*")
    .eq("id", "123")
    .single()
    .execute();
```

### UPDATE

```cpp
// Update com filtro
auto resp = db.from("devices")
    .update(R"({"status": "inactive"})")
    .eq("id", "123")
    .execute();

// Update múltiplos registros
auto resp = db.from("devices")
    .update(R"({"last_seen": "2025-01-01"})")
    .eq("type", "sensor")
    .execute();
```

### DELETE

```cpp
// Delete com filtro
auto resp = db.from("logs")
    .delete_()
    .lt("created_at", "2024-01-01")
    .execute();
```

### UPSERT

```cpp
// Upsert (insert ou update baseado em conflito)
auto resp = db.from("device_status")
    .upsert(
        R"({"device_id": "ABC123", "online": true, "last_seen": "2025-01-01"})",
        "device_id"  // coluna com constraint unique
    );
```

### RPC (Remote Procedure Call)

```cpp
// Chamar função do banco
auto resp = db.rpc("get_device_stats", R"({"period": "day"})");

if (resp.isOk()) {
    ESP_LOGI("DB", "Stats: %s", resp.body.c_str());
}
```

---

## Query Builder

O `SupabaseQuery` fornece uma interface fluente para construção de queries:

### Filtros Disponíveis

| Método | Operador | Descrição |
|--------|----------|-----------|
| `eq(col, val)` | `=` | Igual |
| `neq(col, val)` | `!=` | Diferente |
| `gt(col, val)` | `>` | Maior que |
| `gte(col, val)` | `>=` | Maior ou igual |
| `lt(col, val)` | `<` | Menor que |
| `lte(col, val)` | `<=` | Menor ou igual |
| `like(col, val)` | `LIKE` | Pattern matching |
| `ilike(col, val)` | `ILIKE` | Pattern (case-insensitive) |
| `in_(col, vals)` | `IN` | Valor em lista |
| `is_(col, val)` | `IS` | IS NULL, IS TRUE, etc |

### Ordenação e Paginação

```cpp
auto resp = db.from("devices")
    .select("*")
    .order("created_at", false)  // DESC
    .range(0, 9)                 // Primeiros 10 registros
    .execute();

// Com contagem total
auto resp = db.from("devices")
    .select("*")
    .count(CountOption::Exact)
    .execute();

ESP_LOGI("DB", "Total: %d", resp.count);
```

### Encadeamento de Filtros

```cpp
auto resp = db.from("readings")
    .select("id,value,timestamp")
    .eq("device_id", "sensor-01")
    .gte("timestamp", "2024-01-01")
    .lt("timestamp", "2024-02-01")
    .gt("value", "20")
    .order("timestamp", false)
    .limit(100)
    .execute();
```

---

## SupabaseAuth

Módulo de autenticação para login, signup e gerenciamento de tokens.

### Inicialização

```cpp
#include "SupabaseAuth.h"

using namespace Supabase;

auto& auth = SupabaseAuth::instance();
auth.init("https://xxxxx.supabase.co", "anon-key");
```

### Signup

```cpp
// Signup básico
auto resp = auth.signUp("user@email.com", "password123");

if (resp.isOk()) {
    ESP_LOGI("Auth", "Usuário criado: %s", resp.session.user.id.c_str());
}

// Signup com metadata
SignupOptions opts;
opts.userData["display_name"] = "João Silva";
opts.userData["company"] = "ACME";

auto resp = auth.signUp("user@email.com", "password123", opts);
```

### Login

```cpp
auto resp = auth.signInWithPassword("user@email.com", "password123");

if (resp.isOk()) {
    // Salvar tokens para uso posterior
    std::string accessToken = resp.session.tokens.accessToken;
    std::string refreshToken = resp.session.tokens.refreshToken;
    
    ESP_LOGI("Auth", "Logado como: %s", resp.session.user.email.c_str());
}
```

### Refresh Token

```cpp
// Renovar sessão expirada
auto resp = auth.refreshSession(refreshToken);

if (resp.isOk()) {
    // Usar novos tokens
    accessToken = resp.session.tokens.accessToken;
    refreshToken = resp.session.tokens.refreshToken;
}
```

### Auto-Refresh

```cpp
// Habilitar refresh automático
auth.setAutoRefresh(true, currentSession);

// Eventos de refresh
auth.onTokenRefreshed.addHandler([](const AuthTokens& tokens) {
    ESP_LOGI("Auth", "Token renovado automaticamente");
});
```

### Logout

```cpp
auth.signOut();
```

### Gerenciamento de Usuário

```cpp
// Obter informações do usuário
auto resp = auth.getUser(accessToken);

// Atualizar usuário
auth.updateUser(accessToken, "", "newPassword123");

// Reset de senha
auth.resetPasswordForEmail("user@email.com");
```

---

## Row Level Security (RLS)

Para operações que respeitam RLS, use clientes autenticados:

### Criar Cliente Autenticado

```cpp
// Método 1: A partir do singleton
auto authClient = SupabaseClient::createWithAuth(accessToken);

// Método 2: A partir de instância existente
auto authClient = supabase.withAuth(accessToken);

// Operações respeitam RLS
auto resp = authClient->from("user_data")
    .select("*")
    .execute();
```

### Definir Token no Singleton

```cpp
// Usar token no cliente singleton (afeta todas as operações)
supabase.setAccessToken(accessToken);

// Limpar token (voltar a usar anon key)
supabase.clearAccessToken();
```

---

## Exemplos

### IoT: Enviar Leitura de Sensor

```cpp
void sendReading(float temperature, float humidity) {
    auto& db = SupabaseClient::instance();
    
    char json[128];
    snprintf(json, sizeof(json),
        R"({"device_id":"%s","temperature":%.2f,"humidity":%.2f})",
        deviceId, temperature, humidity);
    
    auto resp = db.from("readings").insert(json);
    
    if (!resp.isOk()) {
        ESP_LOGE("IoT", "Erro ao enviar: %s", resp.error.c_str());
    }
}
```

### Sistema de Configuração Remota

```cpp
bool loadConfig() {
    auto& db = SupabaseClient::instance();
    
    auto resp = db.from("device_config")
        .select("*")
        .eq("device_id", deviceId)
        .single()
        .execute();
    
    if (resp.isOk()) {
        // Parse JSON e aplicar configuração
        return true;
    }
    return false;
}
```

### Autenticação de Dispositivo

```cpp
void authenticateDevice() {
    auto& auth = SupabaseAuth::instance();
    
    // Login do dispositivo
    auto resp = auth.signInWithPassword(deviceEmail, devicePassword);
    
    if (resp.isOk()) {
        // Usar cliente autenticado para operações RLS
        auto client = SupabaseClient::createWithAuth(
            resp.session.tokens.accessToken
        );
        
        // Agora operações respeitam RLS
        auto data = client->from("device_data").select("*").execute();
    }
}
```

---

## Tratamento de Erros

```cpp
auto resp = db.from("table").select("*").execute();

if (!resp.isOk()) {
    ESP_LOGE("DB", "Erro HTTP %d: %s", resp.statusCode, resp.error.c_str());
    
    if (resp.isNotFound()) {
        // Tabela ou registro não encontrado
    } else if (resp.isUnauthorized()) {
        // Token inválido ou expirado
    } else if (resp.isForbidden()) {
        // Sem permissão (RLS)
    }
}
```

---

## Referências

- [Supabase Documentation](https://supabase.com/docs)
- [PostgREST Reference](https://postgrest.org/en/stable/references/api.html)
- [Supabase Auth](https://supabase.com/docs/guides/auth)
