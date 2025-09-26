// VOL_0004: Temporal accumulation compute shader
// Blends current frame with history buffer using exponential moving average

Texture2D<float4> g_currentFrame : register(t0);
Texture2D<float4> g_historyFrame : register(t1);
RWTexture2D<float4> g_output : register(u0);

cbuffer AccumulationConstants : register(b0) {
    float g_alpha;           // Blend factor: 0 = all history, 1 = all current
    float g_resetHistory;    // 1.0 to reset, 0.0 for normal accumulation
    float2 g_jitter;         // Sub-pixel jitter offset for TAA
    float2 g_screenSize;
    float2 g_padding;
};

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    // Early exit for out-of-bounds threads
    if (any(id.xy >= uint2(g_screenSize))) {
        return;
    }

    // Sample current frame
    float4 current = g_currentFrame[id.xy];

    // If resetting history or first frame, use current frame directly
    if (g_resetHistory > 0.5) {
        g_output[id.xy] = current;
        return;
    }

    // Sample history (could add motion vectors here for better reprojection)
    float4 history = g_historyFrame[id.xy];

    // Exponential moving average blend
    // newHistory = alpha * current + (1 - alpha) * history
    float4 accumulated = lerp(history, current, g_alpha);

    // Clamp to prevent fireflies (bright pixel accumulation)
    accumulated.rgb = min(accumulated.rgb, 100.0);

    // Write result
    g_output[id.xy] = accumulated;
}