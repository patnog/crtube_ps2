#include "localization.h"

static crtube_language_t g_language = CRTUBE_LANGUAGE_ENGLISH;

static const char *const g_text[CRTUBE_LANGUAGE_COUNT][CRTUBE_TEXT_COUNT] = {
    {
        "CRTube for PlayStation 2",
        "Main Menu",
        "Search Videos",
        "Network Status",
        "About CRTube",
        "UP / DOWN: navigate",
        "X: select",
        "SELECT + START: exit",
        "Search Videos",
        "Video search is not available yet.",
        "HTTPS/TLS support is required first.",
        "Next: test YouTube HTTPS and JSON.",
        "CIRCLE: back",
        "Network Status",
        "Status: ready",
        "Status: failed (%d)",
        "IP address: %s",
        "IP address: unavailable",
        "TRIANGLE: run network tests",
        "Build with: make youtube-test",
        "Running network tests...",
        "Tests complete. Check the SMB log.",
        "Tests complete. SMB log unavailable.",
        "About CRTube",
        "CRTube is a PlayStation 2 homebrew project.",
        "Goal: search and play online videos.",
        "Network initialization is implemented.",
        "YouTube HTTPS/JSON is being tested.",
        "Video playback is not implemented yet.",
        "Preparing diagnostic log...",
        "Loading PS2HTTP driver...",
        "Testing HTTP connection to 1.1.1.1...",
        "Testing PS2IPS loopback...",
        "Testing local PS2IP interface...",
        "Testing gateway TCP connection...",
        "Resolving www.youtube.com...",
        "Testing TCP connection to YouTube port 443...",
        "Testing TCP connection to 1.1.1.1 port 443...",
        "Connecting to YouTube HTTPS...",
        "Requesting YouTube search JSON...",
        "Finishing network diagnostics..."
    },
    {
        "CRTube para PlayStation 2",
        "Menu Principal",
        "Buscar Videos",
        "Estado da Rede",
        "Sobre o CRTube",
        "CIMA / BAIXO: navegar",
        "X: selecionar",
        "SELECT + START: sair",
        "Buscar Videos",
        "A busca de videos ainda nao esta disponivel.",
        "O suporte HTTPS/TLS e necessario.",
        "Proximo: testar HTTPS e JSON do YouTube.",
        "CIRCULO: voltar",
        "Estado da Rede",
        "Estado: pronto",
        "Estado: falha (%d)",
        "Endereco IP: %s",
        "Endereco IP: indisponivel",
        "TRIANGULO: executar testes de rede",
        "Compile com: make youtube-test",
        "Executando testes de rede...",
        "Testes concluidos. Verifique o log SMB.",
        "Testes concluidos. Log SMB indisponivel.",
        "Sobre o CRTube",
        "CRTube e um projeto homebrew para PS2.",
        "Objetivo: buscar e reproduzir videos online.",
        "Inicializacao de rede implementada.",
        "HTTPS/JSON do YouTube em teste.",
        "Reproducao de video ainda nao implementada.",
        "Preparando log de diagnostico...",
        "Carregando driver PS2HTTP...",
        "Testando conexao HTTP com 1.1.1.1...",
        "Testando loopback do PS2IPS...",
        "Testando interface local PS2IP...",
        "Testando conexao TCP com o gateway...",
        "Resolvendo www.youtube.com...",
        "Testando conexao TCP com YouTube na porta 443...",
        "Testando conexao TCP com 1.1.1.1 na porta 443...",
        "Conectando ao HTTPS do YouTube...",
        "Solicitando JSON de pesquisa do YouTube...",
        "Finalizando diagnosticos de rede..."
    }
};

void crtube_set_language(crtube_language_t language)
{
    if (language >= CRTUBE_LANGUAGE_ENGLISH &&
        language < CRTUBE_LANGUAGE_COUNT)
        g_language = language;
}

crtube_language_t crtube_get_language(void)
{
    return g_language;
}

const char *crtube_text(crtube_text_id_t id)
{
    if (id < 0 || id >= CRTUBE_TEXT_COUNT)
        return "";
    return g_text[g_language][id];
}
