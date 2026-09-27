# Sistema de Autenticação - UserManaging v2.0

Sistema moderno de autenticação com suporte a armazenamento local (NVS) e cloud (Supabase), projetado para ESP-IDF 6.x com arquitetura event-driven.

## Índice

- [Características](#características)
- [Arquitetura](#arquitetura)
- [Configuração](#configuração)
- [Uso Básico](#uso-básico)
- [Providers](#providers)
- [Sessões](#sessões)
- [Sincronização](#sincronização)
- [Eventos](#eventos)
- [Migração](#migração)
- [API Reference](#api-reference)

---

## Características

- **Dual Provider**: Suporte a autenticação local (NVS) e cloud (Supabase)
- **Event-Driven**: Todos os eventos via `Event<Args...>`
- **Multi-Sessão**: Suporte a múltiplos usuários simultâneos
- **JWT**: Gerenciamento automático de tokens com auto-refresh
- **Sincronização**: Sync bidirecional entre local e cloud
- **Seguro**: Hash de senhas com SHA-256, proteção contra brute-force
- **Persistente**: Sessões persistem entre reinicializações
- **Configurável**: Flags de compile-time para habilitar/desabilitar features

---

## Arquitetura

```
┌───────────────────────────────────────────────────────────────┐
│                        AuthManager                            │
│  (Singleton - Orquestra autenticação e sessões)              │
├───────────────────────────────────────────────────────────────┤
│  Events: onLogin, onLogout, onSessionExpired, onSyncComplete │
└────────────────────────┬──────────────────────────────────────┘
                         │
          ┌──────────────┴──────────────┐
          │                             │
          ▼                             ▼
┌─────────────────────┐      ┌─────────────────────┐
│   LocalAuthProvider │      │  CloudAuthProvider  │
│   (NVS Storage)     │      │  (Supabase)         │
├─────────────────────┤      ├─────────────────────┤
│ - Hash SHA-256      │      │ - SupabaseAuth      │
│ - Offline auth      │      │ - SupabaseClient    │
│ - Brute-force prot  │      │ - Auto-refresh      │
└─────────────────────┘      └──────────┬──────────┘
          │                             │
          │                             ▼
          │                  ┌─────────────────────┐
          │                  │     Supabase/       │
          │                  ├─────────────────────┤
          │                  │ SupabaseAuth        │
          │                  │ - Login/Signup      │
          │                  │ - Token refresh     │
          │                  ├─────────────────────┤
          │                  │ SupabaseClient      │
          │                  │ - CRUD operations   │
          │                  │ - RLS support       │
          │                  │ - Query builder     │
          │                  └─────────────────────┘
          │                             │
          └──────────────┬──────────────┘
                         ▼
              ┌─────────────────────┐
              │    UserSession      │
              │  (Gerencia sessão)  │
              ├─────────────────────┤
              │ - User info         │
              │ - JWT tokens        │
              │ - Supabase access   │
              │ - Lifecycle events  │
              └─────────────────────┘
```

### Componentes

| Componente | Descrição |
|------------|-----------|
| `AuthManager` | Singleton principal, orquestra autenticação |
| `LocalAuthProvider` | Autenticação via NVS local |
| `CloudAuthProvider` | Autenticação via Supabase (usa SupabaseAuth) |
| `UserSession` | Gerencia sessão individual com acesso ao SupabaseClient |
| `AuthTypes.h` | Tipos, enums e structs |
| `AuthErrorCodes.h` | Códigos de erro específicos |
| `Supabase/SupabaseAuth` | Módulo de autenticação do Supabase |
| `Supabase/SupabaseClient` | Cliente para operações de banco de dados |

---

## Configuração

### Flags de Compile-Time

No seu `projectConfig.h` ou via Kconfig:

```cpp
// Habilitar/desabilitar providers
#define AUTH_LOCAL_ENABLED 1      // Provider local (NVS)
#define AUTH_CLOUD_ENABLED 1      // Provider cloud (Supabase)

// Transporte (opcional)
#define AUTH_BLUETOOTH_ENABLED 0  // Auth via Bluetooth
#define AUTH_WIFI_ENABLED 1       // Auth via WiFi/HTTP

// Features
#define AUTH_REALTIME_ENABLED 0   // Supabase Realtime
```

### Configuração do Supabase

```cpp
// Defina as credenciais
#define AUTH_SUPABASE_URL "https://xxxx.supabase.co"
#define AUTH_SUPABASE_ANON_KEY "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."

// Ou configure em runtime
AuthManager::instance().setSupabaseConfig(
    "https://xxxx.supabase.co",
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."
);
```

### Schema do Supabase

Execute no SQL Editor do Supabase:

```sql
-- Tabela de perfis
CREATE TABLE profiles (
    id UUID PRIMARY KEY REFERENCES auth.users(id) ON DELETE CASCADE,
    email TEXT,
    display_name TEXT,
    role TEXT DEFAULT 'user',
    is_confirmed BOOLEAN DEFAULT FALSE,
    is_active BOOLEAN DEFAULT TRUE,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW()
);

-- Enable RLS
ALTER TABLE profiles ENABLE ROW LEVEL SECURITY;

-- Policy: usuários podem ver seus próprios perfis
CREATE POLICY "Users can view own profile" ON profiles
    FOR SELECT USING (auth.uid() = id);

-- Policy: usuários podem atualizar seus próprios perfis
CREATE POLICY "Users can update own profile" ON profiles
    FOR UPDATE USING (auth.uid() = id);

-- Trigger para criar perfil automaticamente
CREATE OR REPLACE FUNCTION public.handle_new_user()
RETURNS TRIGGER AS $$
BEGIN
    INSERT INTO public.profiles (id, email, display_name)
    VALUES (NEW.id, NEW.email, NEW.raw_user_meta_data->>'display_name');
    RETURN NEW;
END;
$$ LANGUAGE plpgsql SECURITY DEFINER;

CREATE TRIGGER on_auth_user_created
    AFTER INSERT ON auth.users
    FOR EACH ROW EXECUTE FUNCTION public.handle_new_user();
```

---

## Uso Básico

### Inicialização

```cpp
#include "AuthManager.h"

void app_main() {
    // Inicializar com ambos providers
    AuthManager::instance().init(AuthProvider::Both);
    
    // Configurar Supabase
    AuthManager::instance().setSupabaseConfig(
        AUTH_SUPABASE_URL,
        AUTH_SUPABASE_ANON_KEY
    );
    
    // Registrar handlers de eventos
    AuthManager::instance().onLogin.addHandler([](UserSession* session) {
        ESP_LOGI("App", "Usuário logado: %s", session->getEmail().c_str());
        ESP_LOGI("App", "Admin: %s", session->isAdmin() ? "Sim" : "Não");
    });
    
    AuthManager::instance().onLoginFailed.addHandler([](const std::string& email, ErrorCode err) {
        ESP_LOGE("App", "Login falhou para %s: %s", email.c_str(), err.description().c_str());
    });
    
    AuthManager::instance().onSessionExpired.addHandler([](UserSession* session) {
        ESP_LOGW("App", "Sessão expirada: %s", session->getEmail().c_str());
    });
}
```

### Login

```cpp
// Login assíncrono (não bloqueia)
AuthManager::instance().login("usuario@email.com", "senha123");

// Login síncrono (bloqueia)
AuthCredentials creds("usuario@email.com", "senha123");
AuthResult result = AuthManager::instance().loginSync(creds);

if (result.isSuccess()) {
    ESP_LOGI("App", "Logado como %s", result.session.user.email.c_str());
} else {
    ESP_LOGE("App", "Erro: %s", result.error.description().c_str());
}
```

### Cadastro

```cpp
// Cadastro simples
AuthManager::instance().signup("novo@email.com", "senha123", "Nome do Usuário");

// Cadastro com perfil completo
AuthCredentials creds;
creds.email = "novo@email.com";
creds.password = "senha123";
creds.displayName = "Nome do Usuário";

UserInfo profile;
profile.metadata["phone"] = "+55 11 99999-9999";
profile.metadata["company"] = "Minha Empresa";

AuthManager::instance().signup(creds, profile);
```

### Verificar Estado

```cpp
if (AuthManager::instance().isLoggedIn()) {
    UserSession* session = AuthManager::instance().getCurrentSession();
    
    ESP_LOGI("App", "Usuário: %s", session->getDisplayName().c_str());
    ESP_LOGI("App", "Email: %s", session->getEmail().c_str());
    ESP_LOGI("App", "Admin: %s", session->isAdmin() ? "Sim" : "Não");
    
    // Usar token para chamadas API
    if (session->getProvider() == AuthProvider::Cloud) {
        httpClient.setBearerAuth(session->getAccessToken());
    }
}
```

### Logout

```cpp
// Logout do usuário atual
AuthManager::instance().logout();

// Logout de usuário específico
AuthManager::instance().logout("user-id-aqui");

// Logout de todos
AuthManager::instance().logoutAll();
```

---

## Providers

### LocalAuthProvider

Provider para autenticação offline usando NVS:

- **Hash de Senhas**: SHA-256 com salt único por usuário
- **Proteção Brute-Force**: Bloqueio após 5 tentativas (5 min)
- **Primeiro Admin**: Primeiro usuário cadastrado vira admin
- **Confirmação**: Usuários não-admin precisam de aprovação

```cpp
// Usar apenas local
AuthManager::instance().init(AuthProvider::Local);
```

### CloudAuthProvider

Provider para autenticação via Supabase com integração completa:

- **SupabaseAuth**: Usa o módulo dedicado para login/signup/refresh
- **JWT Tokens**: Access + Refresh tokens com auto-refresh
- **SupabaseClient**: Acesso autenticado ao banco de dados
- **RLS**: Suporte completo a Row Level Security

```cpp
// Usar apenas cloud
AuthManager::instance().init(AuthProvider::Cloud);
AuthManager::instance().setSupabaseConfig(url, key);

// Após login, acessar banco de dados autenticado
UserSession* session = AuthManager::instance().getCurrentSession();
auto client = session->getSupabaseClient();
auto resp = client->from("my_data").select("*").execute();
```

### Ambos Providers

```cpp
// Usar ambos com sync
AuthManager::instance().init(AuthProvider::Both);
AuthManager::instance().setAutoSync(true, 300); // Sync a cada 5 min
```

---

## Sessões

### Multi-Sessão

O sistema suporta múltiplos usuários logados simultaneamente:

```cpp
// Configurar máximo de sessões
AuthManager::instance().setMaxSessions(5);

// Obter todas as sessões ativas
auto sessions = AuthManager::instance().getActiveSessions();
for (auto* session : sessions) {
    ESP_LOGI("App", "Sessão: %s", session->getEmail().c_str());
}

// Trocar sessão atual
AuthManager::instance().setCurrentSession("outro-user-id");
```

### Persistência

Sessões podem persistir entre reinicializações:

```cpp
// Habilitar persistência
AuthManager::instance().setPersistSessions(true);

// Após reboot, sessões são restauradas automaticamente
```

### UserSession

Cada sessão oferece:

```cpp
UserSession* session = AuthManager::instance().getCurrentSession();

// Estado
bool valid = session->isValid();
bool expired = session->isExpired();
bool admin = session->isAdmin();

// Info do usuário
const std::string& id = session->getUserId();
const std::string& email = session->getEmail();
const std::string& name = session->getDisplayName();
UserRole role = session->getRole();

// Tokens (para cloud)
const std::string& accessToken = session->getAccessToken();
uint32_t secondsLeft = session->getSecondsUntilExpiry();

// Metadata
std::string phone = session->getMetadataValue("phone", "");

// Eventos da sessão
session->onExpired.addHandler([](UserSession* s) {
    ESP_LOGW("App", "Sessão expirou!");
});
```

### Acesso Autenticado ao Supabase

A partir da sessão, você pode obter um `SupabaseClient` autenticado para realizar operações no banco de dados respeitando Row Level Security (RLS):

```cpp
UserSession* session = AuthManager::instance().getCurrentSession();

if (session && session->hasSupabaseAccess()) {
    // Obter cliente autenticado
    auto client = session->getSupabaseClient();
    
    if (client) {
        // Operações respeitam RLS - usuário só vê seus dados
        auto resp = client->from("user_data")
            .select("*")
            .execute();
        
        if (resp.isOk()) {
            ESP_LOGI("App", "Dados: %s", resp.body.c_str());
        }
        
        // Inserir dados como o usuário autenticado
        resp = client->from("user_logs")
            .insert(R"({"action": "login", "timestamp": "2025-01-01"})");
    }
}
```

### Acesso Direto ao SupabaseAuth

Para operações avançadas de autenticação:

```cpp
#include "SupabaseAuth.h"

// Acesso direto ao módulo de auth
auto& supaAuth = Supabase::SupabaseAuth::instance();

// Verificar sessão atual
if (supaAuth.hasActiveSession()) {
    const auto& authSession = supaAuth.getCurrentSession();
    ESP_LOGI("App", "User: %s", authSession.user.email.c_str());
}

// Eventos do SupabaseAuth
supaAuth.onTokenRefreshed.addHandler([](const Supabase::AuthTokens& tokens) {
    ESP_LOGI("App", "Token renovado, expira em %d segundos", tokens.expiresIn);
});
```

---

## Sincronização

### Sync Manual

```cpp
// Sincronizar com cloud
AuthManager::instance().syncWithCloud();

// Verificar status
const SyncStatus& status = AuthManager::instance().getSyncStatus();
ESP_LOGI("Sync", "Última sync: %llu", status.lastSyncAt);
ESP_LOGI("Sync", "Usuários baixados: %u", status.usersDownloaded);
```

### Auto-Sync

```cpp
// Habilitar sync automático
AuthManager::instance().setAutoSync(true, 300); // A cada 5 minutos

// Eventos de sync
AuthManager::instance().onSyncComplete.addHandler([](const SyncStatus& status) {
    ESP_LOGI("Sync", "Sync completo!");
});

AuthManager::instance().onSyncFailed.addHandler([](ErrorCode err) {
    ESP_LOGE("Sync", "Sync falhou: %s", err.description().c_str());
});
```

### Resolução de Conflitos

Por padrão, dados do cloud têm prioridade:

```cpp
AuthConfig config;
config.conflictResolution = SyncConflictResolution::CloudWins;  // Padrão
// ou
config.conflictResolution = SyncConflictResolution::LocalWins;
config.conflictResolution = SyncConflictResolution::NewestWins;

AuthManager::instance().init(config);
```

---

## Eventos

### Eventos Disponíveis

| Evento | Parâmetros | Descrição |
|--------|------------|-----------|
| `onLogin` | `UserSession*` | Login bem-sucedido |
| `onLogout` | `UserSession*` | Logout realizado |
| `onLoginFailed` | `email, ErrorCode` | Login falhou |
| `onSignup` | `UserSession*` | Cadastro bem-sucedido |
| `onSignupFailed` | `email, ErrorCode` | Cadastro falhou |
| `onSessionExpired` | `UserSession*` | Sessão expirou |
| `onTokenRefreshed` | `UserSession*` | Token renovado |
| `onStateChanged` | `oldState, newState` | Estado mudou |
| `onSyncComplete` | `SyncStatus` | Sync concluída |
| `onSyncFailed` | `ErrorCode` | Sync falhou |

### Exemplo de Uso

```cpp
AuthManager& auth = AuthManager::instance();

auth.onStateChanged.addHandler([](AuthState oldState, AuthState newState) {
    ESP_LOGI("Auth", "Estado: %s -> %s", 
             authStateToString(oldState),
             authStateToString(newState));
});

auth.onTokenRefreshed.addHandler([](UserSession* session) {
    ESP_LOGI("Auth", "Token renovado para %s", session->getEmail().c_str());
    // Atualizar token em chamadas HTTP
    httpClient.setBearerAuth(session->getAccessToken());
});
```

---

## Gerenciamento de Usuários

### Funções Administrativas

```cpp
// Requer sessão de admin

// Listar usuários
std::vector<UserInfo> users;
AuthManager::instance().getAllUsers(users);

// Listar pendentes de confirmação
std::vector<UserInfo> pending;
AuthManager::instance().getPendingUsers(pending);

// Confirmar usuário
AuthManager::instance().confirmUser("user-id");

// Alterar role
AuthManager::instance().setUserRole("user-id", UserRole::Admin);

// Desativar usuário
AuthManager::instance().setUserActive("user-id", false);

// Excluir usuário
AuthManager::instance().deleteUser("user-id");
```

### Alteração de Senha

```cpp
// Alterar senha do usuário logado
ErrorCode err = AuthManager::instance().changePassword("senhaAntiga", "senhaNova");

// Solicitar reset de senha (cloud only)
AuthManager::instance().requestPasswordReset("email@exemplo.com");
```

---

## Migração do Sistema Antigo

### De UserManager para AuthManager

**Antes (UserManager):**

```cpp
UserManager manager;
manager.CreateManager();
manager.Login(data, connection);
```

**Depois (AuthManager):**

```cpp
AuthManager::instance().init(AuthProvider::Both);
AuthManager::instance().login("email@exemplo.com", "senha");
```

### Mapeamento de Classes

| Antigo | Novo |
|--------|------|
| `UserManager` | `AuthManager` |
| `ConnectedUser` | `UserSession` |
| `SimpleUser` | `UserSession` |
| `SimpleUserManager` | `AuthManager` |

### Mapeamento de Eventos

| Antigo | Novo |
|--------|------|
| `PressedEvent` | `onLogin` |
| `LogoffEvent` | `onLogout` |
| `DisconnectEvent` | `onSessionExpired` |

---

## Códigos de Erro

### Principais Erros

| Código | Descrição |
|--------|-----------|
| `Auth_None` | Sucesso |
| `Auth_NotInitialized` | Sistema não inicializado |
| `Auth_UserNotFound` | Usuário não encontrado |
| `Auth_WrongPassword` | Senha incorreta |
| `Auth_AccountNotConfirmed` | Conta pendente de aprovação |
| `Auth_AccountLocked` | Conta bloqueada |
| `Auth_EmailAlreadyExists` | Email já cadastrado |
| `Auth_AccessTokenExpired` | Token expirado |
| `Auth_NoNetwork` | Sem conexão de rede |
| `Auth_CloudRequestFailed` | Falha na requisição cloud |

### Verificar Erros

```cpp
AuthResult result = AuthManager::instance().loginSync(creds);

if (result.error == AuthErrorCodes::WrongPassword) {
    ESP_LOGE("Auth", "Senha incorreta!");
} else if (result.error == AuthErrorCodes::AccountLocked) {
    ESP_LOGE("Auth", "Conta bloqueada! Aguarde 5 minutos.");
} else if (result.error == AuthErrorCodes::AccountNotConfirmed) {
    ESP_LOGW("Auth", "Aguarde aprovação do administrador.");
}
```

---

## Referências

- [Supabase Auth Documentation](https://supabase.com/docs/guides/auth)
- [Supabase JavaScript Client](https://supabase.com/docs/reference/javascript/introduction)
- [ESP-IDF NVS Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html)
