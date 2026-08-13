// vbcpet — soullink-emotion SDK 最小验证 demo (Node 无渲染)
// 目的: 1) 给 Hiyori 生成 ModelProfile  2) 跑 engine 触发情绪 → 输出 Cubism 参数
// 用法: cd live2d && node demo_soullink.mjs
import { Live2DProfileAutoGenerator } from "@soullink-emotion/profile-generator";
import { SoullinkRuntime, getVADPreset, motionStylePresets } from "@soullink-emotion/engine";

// ── 1. 给 Hiyori 生成 ModelProfile (FACS 语义 → Hiyori 真实 Cubism 参数 ID) ──
const generator = new Live2DProfileAutoGenerator({
  modelsRoot: "d:/vscode/vbcpet/live2d",
  modelsBaseUrl: "/models",
  useConfiguredOpenAI: false,   // 确定性标准映射, 不走 LLM
});

console.log("生成 Hiyori profile ...");
const { profile } = await generator.ensure({
  modelDir: "Hiyori",
  displayName: "Hiyori",
  force: true,
});

console.log("\n=== profile 生成结果 ===");
console.log("modelPath   :", profile.modelPath);
console.log("paramMap    :", Object.keys(profile.parameterMap).join(", "));
console.log("expressionMap:", JSON.stringify(profile.expressionMap ?? {}, null, 1).slice(0, 600));
console.log("motionMap   :", JSON.stringify(profile.motionMap ?? {}).slice(0, 300));
console.log("nativeAnim  :", JSON.stringify(profile.nativeAnimations ?? {}).slice(0, 200));

// ── 2. 纯 engine: 触发情绪 → 跑 60 帧 → 读 live2dParams ──
const runtime = new SoullinkRuntime({
  profile,
  motionStyle: { ...motionStylePresets.natural },
});

const vad = getVADPreset("happy", "bright_smile");
console.log(`\n=== trigger happy/bright_smile, VAD=${JSON.stringify(vad)} ===`);
runtime.triggerIntent(
  { emotion: "happy", variant: "bright_smile", naturalVAD: vad, intensity: 0.8, contextTags: ["manual"] },
  0,
  { vadTarget: vad },
);

for (let i = 1; i <= 60; i++) {
  const snap = runtime.update(i / 60, 1 / 60);
  if ([1, 5, 15, 30, 60].includes(i)) {
    const p = snap.live2dParams;
    const keys = Object.keys(p);
    const show = keys.slice(0, 10);
    console.log(`frame ${String(i).padStart(2)} (${keys.length} params): ` +
      show.map(k => `${k}=${p[k].toFixed(2)}`).join("  "));
  }
}

console.log("\n=== demo 通过: engine 在 Node 下可跑, 输出了 Hiyori 可消费的参数 ===");
