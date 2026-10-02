// Wire de teste do Chat IA (nunca vai ao aparelho): conecta o WiFi ficticio,
// scripta a resposta da IA e digita uma mensagem. Usado por
// `node tools/sdk/celer.js test|emu "data/apps/Chat IA"`.
module.exports.wire = function (env) {
    env.Net.isConnected = function () { return true; };
    env.__harness.setAiResponse({
        ok: true,
        status: 200,
        content: 'Ola! Sou o assistente do CelerOS.',
        usage: { prompt_tokens: 12, completion_tokens: 9, total_tokens: 21 },
        raw: '{"choices":[{"message":{"content":"Ola!"}}]}'
    });
    env.__harness.typeLine('oi');
};
