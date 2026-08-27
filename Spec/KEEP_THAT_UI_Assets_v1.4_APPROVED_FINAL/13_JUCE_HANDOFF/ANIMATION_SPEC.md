# Animation Specification
- HUD orbit: 64-frame loop, 60 FPS normal, 30 FPS low power.
- HUD pulse: 32-frame loop while armed.
- Meters: 60 FPS smoothing with peak hold and decay.
- Waveforms: 30–60 FPS depending on host CPU load.
- Button glow: 120–180 ms transition.
- Keep Last success flash: 280 ms red-to-gold pulse.
- Recent Keeps insertion: 180 ms slide/fade.
- Reduce Motion: disable orbit rotation, keep only meter motion and state fades.
