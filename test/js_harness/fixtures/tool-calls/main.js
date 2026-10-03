// Fixture API 20: AI.chat com tools — valida o contrato do resultado
// (r.toolCalls[{id,name,args}] + r.finishReason) que o firmware monta em
// aiPushResult a partir de choices[0].message.tool_calls.
var ok = AI.chat({
    provider: "openrouter",
    messages: [{ role: "user", content: "manda o cachorro sentar" }],
    tools: [{ type: "function", function: {
        name: "dog_command",
        parameters: { type: "object", properties: {
            command: { type: "string", enum: ["sit", "lie", "stand", "walk"] }
        }, required: ["command"] }
    } }],
    tool_choice: "auto",
    max_tokens: 150
}, function (r) {
    System.print("ok=" + r.ok);
    System.print("finish=" + r.finishReason);
    if (r.ok && r.toolCalls && r.toolCalls.length) {
        var tc = r.toolCalls[0];
        System.print("call=" + tc.name + " cmd=" + (tc.args ? tc.args.command : "?") +
                 " id=" + (tc.id ? "sim" : "nao"));
    } else if (r.ok) {
        System.print("texto=" + r.content);
    } else {
        System.print("erro=" + r.error);
    }
    System.exitApp();
});
if (!ok) System.exitApp();
while (true) System.delay(20);
