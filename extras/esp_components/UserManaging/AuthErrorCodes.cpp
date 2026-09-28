#include "AuthErrorCodes.h"

namespace AuthErrorCodes {

    // ========== General Auth Errors ==========
    
    const ErrorCodeItem None{
        "Auth_None",
        "Operação realizada com sucesso",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem NotInitialized{
        "Auth_NotInitialized",
        "Sistema de autenticação não inicializado",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem InvalidConfig{
        "Auth_InvalidConfig",
        "Configuração inválida fornecida",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem Cancelled{
        "Auth_Cancelled",
        "Operação cancelada pelo usuário",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem Timeout{
        "Auth_Timeout",
        "Operação expirou por tempo limite",
        ErrorCodeType::User
    };
    
    // ========== Login Errors ==========
    
    const ErrorCodeItem UserNotFound{
        "Auth_UserNotFound",
        "Usuário não encontrado",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem WrongPassword{
        "Auth_WrongPassword",
        "Senha incorreta",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem AccountNotConfirmed{
        "Auth_AccountNotConfirmed",
        "Conta não confirmada. Aguarde aprovação do administrador",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem AccountDisabled{
        "Auth_AccountDisabled",
        "Conta desativada",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem AccountLocked{
        "Auth_AccountLocked",
        "Conta bloqueada. Muitas tentativas de login incorretas",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem AlreadyLoggedIn{
        "Auth_AlreadyLoggedIn",
        "Usuário já está logado",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem NotLoggedIn{
        "Auth_NotLoggedIn",
        "Nenhuma sessão ativa",
        ErrorCodeType::User
    };
    
    // ========== Signup Errors ==========
    
    const ErrorCodeItem EmailAlreadyExists{
        "Auth_EmailAlreadyExists",
        "Email já cadastrado",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem InvalidEmail{
        "Auth_InvalidEmail",
        "Formato de email inválido",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem WeakPassword{
        "Auth_WeakPassword",
        "Senha não atende aos requisitos de segurança",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem PasswordTooShort{
        "Auth_PasswordTooShort",
        "Senha muito curta",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem PasswordTooLong{
        "Auth_PasswordTooLong",
        "Senha muito longa",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem InvalidDisplayName{
        "Auth_InvalidDisplayName",
        "Nome de exibição inválido",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem SignupFailed{
        "Auth_SignupFailed",
        "Falha no cadastro do usuário",
        ErrorCodeType::User
    };
    
    // ========== Token Errors ==========
    
    const ErrorCodeItem InvalidAccessToken{
        "Auth_InvalidAccessToken",
        "Token de acesso inválido",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem AccessTokenExpired{
        "Auth_AccessTokenExpired",
        "Token de acesso expirado",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem InvalidRefreshToken{
        "Auth_InvalidRefreshToken",
        "Token de atualização inválido",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem RefreshTokenExpired{
        "Auth_RefreshTokenExpired",
        "Token de atualização expirado",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem TokenRefreshFailed{
        "Auth_TokenRefreshFailed",
        "Falha ao atualizar token",
        ErrorCodeType::User
    };
    
    // ========== Permission Errors ==========
    
    const ErrorCodeItem PermissionDenied{
        "Auth_PermissionDenied",
        "Permissão negada",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem AdminRequired{
        "Auth_AdminRequired",
        "Privilégios de administrador necessários",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem ApprovalRequired{
        "Auth_ApprovalRequired",
        "Aprovação do administrador necessária",
        ErrorCodeType::User
    };
    
    // ========== Session Errors ==========
    
    const ErrorCodeItem InvalidSession{
        "Auth_InvalidSession",
        "Sessão inválida",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem SessionExpired{
        "Auth_SessionExpired",
        "Sessão expirada",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem MaxSessionsReached{
        "Auth_MaxSessionsReached",
        "Número máximo de sessões simultâneas atingido",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem SessionNotFound{
        "Auth_SessionNotFound",
        "Sessão não encontrada",
        ErrorCodeType::User
    };
    
    // ========== Cloud/Network Errors ==========
    
    const ErrorCodeItem NoNetwork{
        "Auth_NoNetwork",
        "Sem conexão de rede",
        ErrorCodeType::Communication
    };
    
    const ErrorCodeItem CloudUnavailable{
        "Auth_CloudUnavailable",
        "Serviço em nuvem indisponível",
        ErrorCodeType::Communication
    };
    
    const ErrorCodeItem CloudRequestFailed{
        "Auth_CloudRequestFailed",
        "Falha na requisição ao serviço em nuvem",
        ErrorCodeType::Communication
    };
    
    const ErrorCodeItem InvalidCloudResponse{
        "Auth_InvalidCloudResponse",
        "Resposta inválida do serviço em nuvem",
        ErrorCodeType::Communication
    };
    
    const ErrorCodeItem SupabaseNotConfigured{
        "Auth_SupabaseNotConfigured",
        "Supabase não configurado",
        ErrorCodeType::User
    };
    
    // ========== Sync Errors ==========
    
    const ErrorCodeItem SyncFailed{
        "Auth_SyncFailed",
        "Falha na sincronização",
        ErrorCodeType::Communication
    };
    
    const ErrorCodeItem SyncConflict{
        "Auth_SyncConflict",
        "Conflito de sincronização detectado",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem SyncInProgress{
        "Auth_SyncInProgress",
        "Sincronização já em andamento",
        ErrorCodeType::User
    };
    
    // ========== Storage Errors ==========
    
    const ErrorCodeItem StorageSaveFailed{
        "Auth_StorageSaveFailed",
        "Falha ao salvar no armazenamento local",
        ErrorCodeType::Storage
    };
    
    const ErrorCodeItem StorageLoadFailed{
        "Auth_StorageLoadFailed",
        "Falha ao carregar do armazenamento local",
        ErrorCodeType::Storage
    };
    
    const ErrorCodeItem StorageFull{
        "Auth_StorageFull",
        "Armazenamento local cheio",
        ErrorCodeType::Storage
    };
    
    // ========== Validation Errors ==========
    
    const ErrorCodeItem InvalidCredentials{
        "Auth_InvalidCredentials",
        "Credenciais inválidas",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem MissingRequiredField{
        "Auth_MissingRequiredField",
        "Campo obrigatório não preenchido",
        ErrorCodeType::User
    };
    
    const ErrorCodeItem InvalidUserId{
        "Auth_InvalidUserId",
        "ID de usuário inválido",
        ErrorCodeType::User
    };

    void registerAll() {
        ErrorCode::AddErrorItem(None);
        ErrorCode::AddErrorItem(NotInitialized);
        ErrorCode::AddErrorItem(InvalidConfig);
        ErrorCode::AddErrorItem(Cancelled);
        ErrorCode::AddErrorItem(Timeout);
        
        ErrorCode::AddErrorItem(UserNotFound);
        ErrorCode::AddErrorItem(WrongPassword);
        ErrorCode::AddErrorItem(AccountNotConfirmed);
        ErrorCode::AddErrorItem(AccountDisabled);
        ErrorCode::AddErrorItem(AccountLocked);
        ErrorCode::AddErrorItem(AlreadyLoggedIn);
        ErrorCode::AddErrorItem(NotLoggedIn);
        
        ErrorCode::AddErrorItem(EmailAlreadyExists);
        ErrorCode::AddErrorItem(InvalidEmail);
        ErrorCode::AddErrorItem(WeakPassword);
        ErrorCode::AddErrorItem(PasswordTooShort);
        ErrorCode::AddErrorItem(PasswordTooLong);
        ErrorCode::AddErrorItem(InvalidDisplayName);
        ErrorCode::AddErrorItem(SignupFailed);
        
        ErrorCode::AddErrorItem(InvalidAccessToken);
        ErrorCode::AddErrorItem(AccessTokenExpired);
        ErrorCode::AddErrorItem(InvalidRefreshToken);
        ErrorCode::AddErrorItem(RefreshTokenExpired);
        ErrorCode::AddErrorItem(TokenRefreshFailed);
        
        ErrorCode::AddErrorItem(PermissionDenied);
        ErrorCode::AddErrorItem(AdminRequired);
        ErrorCode::AddErrorItem(ApprovalRequired);
        
        ErrorCode::AddErrorItem(InvalidSession);
        ErrorCode::AddErrorItem(SessionExpired);
        ErrorCode::AddErrorItem(MaxSessionsReached);
        ErrorCode::AddErrorItem(SessionNotFound);
        
        ErrorCode::AddErrorItem(NoNetwork);
        ErrorCode::AddErrorItem(CloudUnavailable);
        ErrorCode::AddErrorItem(CloudRequestFailed);
        ErrorCode::AddErrorItem(InvalidCloudResponse);
        ErrorCode::AddErrorItem(SupabaseNotConfigured);
        
        ErrorCode::AddErrorItem(SyncFailed);
        ErrorCode::AddErrorItem(SyncConflict);
        ErrorCode::AddErrorItem(SyncInProgress);
        
        ErrorCode::AddErrorItem(StorageSaveFailed);
        ErrorCode::AddErrorItem(StorageLoadFailed);
        ErrorCode::AddErrorItem(StorageFull);
        
        ErrorCode::AddErrorItem(InvalidCredentials);
        ErrorCode::AddErrorItem(MissingRequiredField);
        ErrorCode::AddErrorItem(InvalidUserId);
    }

} // namespace AuthErrorCodes
