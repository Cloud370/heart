let isScanning = false;
// ECG Animation Globals
let ecgData = []; // Buffer for pixel Y values
let lastBeatTime = 0;
let currentBpm = 0;
let isConnected = false;
let animationId = null;
let hrHistory = new Array(50).fill(0); // History for Trend Graph
let waveformMode = 'ecg'; // 'ecg' or 'trend'

// Standard ECG Wave Pattern (Simplified P-QRS-T)
// Represents relative Y offsets. 0 is baseline.
const QRS_WAVE = [
    -2, -4, 0, // P wave
    0, 0,
    5, -15, 25, -5, 0, // QRS complex (sharp spike)
    0, 0,
    -3, -5, -2, 0 // T wave
];

function startHRPoll() {
    // Start Animation Loop immediately
    startWaveformAnimation();

    setInterval(() => {
        fetch('/api/hr')
            .then(r => r.json())
            .then(data => {
                const connected = data.hr > 0;
                const text = connected ? data.hr : '--';
                
                // Update Globals
                currentBpm = connected ? data.hr : 0;
                isConnected = connected;

                // Update History
                if (connected) {
                    hrHistory.push(data.hr);
                    if (hrHistory.length > 50) hrHistory.shift();
                } else {
                    // Optional: Push 0 or keep last? 
                    // For trend graph, maybe push 0 to show disconnect
                    hrHistory.push(0);
                    if (hrHistory.length > 50) hrHistory.shift();
                }

                const el = document.getElementById('heart-rate-value');
                if (el) el.innerText = text;

                const el2 = document.getElementById('hr-display');
                if (el2) el2.innerText = text;
                
                // Handle animation state
                const visual = document.querySelector('.heart-visual');
                if (visual) {
                    if (connected) {
                        visual.classList.remove('disconnected');
                    } else {
                        visual.classList.add('disconnected');
                    }
                }

                // Update settings page badge
                const badge = document.getElementById('connection-badge');
                const scanBtn = document.getElementById('scan-btn');
                const disconnectBtn = document.getElementById('disconnect-btn');

                if (badge) {
                    if (connected) {
                        badge.innerText = '已连接';
                        badge.classList.remove('disconnected');
                        badge.classList.add('connected');
                        
                        // Connected State: Disable Scan, Enable Disconnect
                        if (scanBtn) scanBtn.disabled = true;
                        if (disconnectBtn) disconnectBtn.disabled = false;
                    } else {
                        badge.innerText = '未连接';
                        badge.classList.remove('connected');
                        badge.classList.add('disconnected');
                        
                        // Disconnected State: Enable Scan (if not currently scanning), Disable Disconnect
                        if (scanBtn && !isScanning) scanBtn.disabled = false;
                        if (disconnectBtn) disconnectBtn.disabled = true;
                    }
                }

                // Update OBS status text
                const obsStatus = document.getElementById('obs-status-text');
                if (obsStatus) {
                    obsStatus.style.display = connected ? 'none' : 'block';
                }
            })
            .catch(e => console.error(e));
    }, 1000);
}

function startThemePoll() {
    let currentThemeStr = '';

    // Poll theme every 1 second
    setInterval(() => {
        // Add timestamp to prevent caching
        fetch('/api/theme?t=' + Date.now(), { cache: "no-store" })
            .then(r => r.json())
            .then(data => {
                if (!data.theme) return;
                if (data.theme === currentThemeStr) return;
                currentThemeStr = data.theme;

                applyTheme(data.theme);
            })
            .catch(e => { });
    }, 1000);
}

function resolveThemeConfig(themeStr) {
    let config = {};
    if (themeStr.startsWith('{')) {
        try {
            config = JSON.parse(themeStr);
        } catch(e) {
            console.error('Error parsing theme JSON', e);
        }
    } else {
        // Legacy fallback
        if (themeStr === 'cyberpunk') {
            config = {
                textColor: '#0ff', font: "'Orbitron', sans-serif", textShadow: '0 0 5px #0ff, 0 0 10px #0ff',
                heartColor: '#f0f', heartFilter: 'drop-shadow(0 0 5px #f0f)',
                pulseColor: '#0ff', pulseBorder: '2px solid #0ff', pulseShadow: '0 0 10px #0ff',
                animation: 'pulse-ring', bgColor: 'transparent'
            };
        } else if (themeStr === 'retro') {
            config = {
                textColor: '#ffcc00', font: "'Press Start 2P', cursive", textShadow: '2px 2px 0 #330000',
                heartColor: '#cc0000', heartFilter: 'drop-shadow(2px 2px 0 #330000)',
                pulseColor: '#cc0000', pulseBorder: '4px solid #cc0000', pulseRadius: '0',
                animation: 'beat', bgColor: 'transparent'
            };
        } else if (themeStr === 'nature') {
            config = {
                textColor: '#2e8b57', font: "'Montserrat', sans-serif", textShadow: '1px 1px 2px rgba(0,0,0,0.2)',
                heartColor: '#2e8b57', 
                pulseColor: 'transparent', pulseBackground: 'rgba(46, 139, 87, 0.2)', pulseBorder: 'none',
                animation: 'pulse-ring', bgColor: 'transparent'
            };
        } else if (themeStr === 'minimal') {
            config = {
                textColor: '#000', font: "'Roboto', sans-serif",
                heartColor: '#000', 
                pulseColor: '#000', pulseBorder: '1px solid #000',
                animation: 'beat', bgColor: 'transparent'
            };
        } else {
            // Default
            config = {
                textColor: '#333333', font: "'Roboto', sans-serif",
                heartColor: '#ff4d4d',
                pulseColor: '#ff4d4d', pulseBorder: '1px solid #ff4d4d',
                animation: 'beat', bgColor: 'transparent'
            };
        }
    }
    return config;
}

function hexToRgba(hex, alpha) {
    let r = 0, g = 0, b = 0;
    // 3 digits
    if (hex.length === 4) {
        r = parseInt(hex[1] + hex[1], 16);
        g = parseInt(hex[2] + hex[2], 16);
        b = parseInt(hex[3] + hex[3], 16);
    }
    // 6 digits
    else if (hex.length === 7) {
        r = parseInt(hex.substring(1, 3), 16);
        g = parseInt(hex.substring(3, 5), 16);
        b = parseInt(hex.substring(5, 7), 16);
    }
    
    if (typeof alpha === 'undefined' || alpha === null) alpha = 1;
    
    return `rgba(${r}, ${g}, ${b}, ${alpha})`;
}

function applyConfigToElement(element, config) {
    if (!element) return;
    
    const layoutMode = config.layoutMode || 'full'; // 'full' or 'card'
    const bgOpacity = config.bgOpacity !== undefined ? config.bgOpacity : 1.0;
    const bgColor = config.bgColor || '#333333';
    
    const rgbaBg = (bgColor === 'transparent') ? 'transparent' : hexToRgba(bgColor, bgOpacity);

    // Apply Styles to container or body based on mode
    const container = element.querySelector('#heart-rate-container') || element.querySelector('.heart-rate-container');
    
    if (layoutMode === 'card' && container) {
        // Card Mode: Body transparent, Container gets style
        element.style.backgroundColor = 'transparent';
        container.style.backgroundColor = rgbaBg;
        container.style.borderRadius = config.boxRadius || '10px';
        container.style.boxShadow = config.boxShadow || '0 4px 6px rgba(0,0,0,0.1)';
        container.style.border = config.boxBorder || 'none';
        container.style.padding = config.boxPadding || '10px 20px';
    } else {
        // Full Mode: Body gets style, Container transparent
        element.style.backgroundColor = rgbaBg;
        if (container) {
            container.style.backgroundColor = 'transparent';
            container.style.borderRadius = '0';
            container.style.boxShadow = 'none';
            container.style.border = 'none';
            container.style.padding = '10px'; // default padding
        }
    }

    element.style.color = config.textColor || '#333';
    element.style.fontFamily = config.font || 'inherit';
    element.style.textShadow = config.textShadow || 'none';

    // Toggle BPM Unit
    const bpmUnit = element.querySelector('#heart-rate-unit');
    if (bpmUnit) {
        bpmUnit.style.display = (config.showBpmText === false) ? 'none' : 'inline';
    }

    // Toggle Waveform
    const waveformCanvas = element.querySelector('#heart-waveform');
    if (waveformCanvas) {
        waveformCanvas.style.display = (config.showWaveform === true) ? 'block' : 'none';
        // Apply color to waveform
        // Prefer specific waveform color, fallback to text color
        waveformCanvas.dataset.color = config.waveformColor || config.textColor || '#333';
        
        // Update global mode if we are applying to body (main view)
        // or if we are applying to preview (we can set a global flag, but usually only one view is active)
        // Since script.js is shared, we should check if we are updating the main config
        // But simply updating the global variable is fine as preview and main view won't conflict in same window
        if (config.waveformMode) {
            waveformMode = config.waveformMode;
        }
    }

    const heartSvg = element.querySelector('.heart-svg');
    if (heartSvg) {
        heartSvg.style.fill = config.heartColor || '#ff4d4d';
        heartSvg.style.filter = config.heartFilter || 'none';
        
        if (config.animation === 'none') {
            heartSvg.style.animation = 'none';
        } else {
            heartSvg.style.animation = 'beat 1s infinite';
        }
    }

    const pulseRing = element.querySelector('.heart-pulse-ring');
    if (pulseRing) {
        if (config.animation === 'none') {
            pulseRing.style.display = 'none';
            pulseRing.style.animation = 'none';
        } else {
            pulseRing.style.display = 'block';
            
            if (config.pulseBorder) {
                pulseRing.style.border = config.pulseBorder;
            } else {
                pulseRing.style.border = '1px solid ' + (config.pulseColor || '#ff4d4d');
            }
            
            pulseRing.style.backgroundColor = config.pulseBackground || 'transparent';
            pulseRing.style.boxShadow = config.pulseShadow || 'none';
            pulseRing.style.borderRadius = config.pulseRadius || '50%';

            if (config.animation === 'pulse-ring') {
                 pulseRing.style.animation = 'pulse-ring 1s cubic-bezier(0.215, 0.61, 0.355, 1) infinite';
            } else if (config.animation === 'beat') {
                 pulseRing.style.display = 'none';
            } else {
                 pulseRing.style.animation = 'pulse-ring 1s cubic-bezier(0.215, 0.61, 0.355, 1) infinite';
            }
        }
    }
}

function applyTheme(themeStr) {
    const config = resolveThemeConfig(themeStr);
    
    // Clear legacy classes
    document.body.className = ''; 
    
    applyConfigToElement(document.body, config);
}

function startScan() {
    const btn = document.getElementById('scan-btn');
    const status = document.getElementById('status');
    btn.disabled = true;
    status.innerText = '扫描中...';
    isScanning = true;

    fetch('/api/scan', { method: 'POST' })
        .then(r => r.json())
        .then(data => {
            console.log('Scan started');
            // Auto stop UI scan state after 10s matches backend
            setTimeout(() => {
                isScanning = false;
                btn.disabled = false;
                status.innerText = '扫描结束';
            }, 10000);
        });
}

function connect(id) {
    const status = document.getElementById('status');
    status.innerText = '连接中...';
    fetch('/api/connect', {
        method: 'POST',
        body: JSON.stringify({ id: id }),
        headers: { 'Content-Type': 'application/json' }
    })
        .then(r => r.json())
        .then(data => {
            status.innerText = '已发送连接请求';
        });
}

function disconnect() {
    if(!confirm('确定要断开连接吗？')) return;
    const status = document.getElementById('status');
    if(status) status.innerText = '正在断开...';
    
    fetch('/api/disconnect', { method: 'POST' })
        .then(r => r.json())
        .then(data => {
            if(status) status.innerText = '已断开';
        });
}

function reset() {
    if(!confirm('确定要重置吗？这将清除保存的设备并断开连接。')) return;
    const status = document.getElementById('status');
    if(status) status.innerText = '正在重置...';

    fetch('/api/reset', { method: 'POST' })
        .then(r => r.json())
        .then(data => {
            if(status) status.innerText = '已重置';
            const list = document.getElementById('device-list');
            if(list) list.innerHTML = '';
        });
}

function startWaveformAnimation() {
    if (animationId) return; // Already running

    // Initialize buffer with baseline
    const canvas = document.getElementById('heart-waveform');
    // Default size if canvas not yet loaded, will be resized in loop
    const width = canvas ? canvas.width : 200; 
    ecgData = new Array(width).fill(0);

    let waveIndex = -1; // -1 means not currently drawing a beat
    let frameCount = 0;
    const speedDivider = 2; // Update every 2 frames to slow down scroll speed

    function loop(timestamp) {
        const canvas = document.getElementById('heart-waveform');
        if (!canvas || canvas.style.display === 'none') {
            requestAnimationFrame(loop);
            return;
        }

        const width = canvas.width;
        const height = canvas.height;
        const ctx = canvas.getContext('2d');

        // Style
        const color = canvas.dataset.color || getComputedStyle(canvas).color || '#333';
        ctx.strokeStyle = color;
        ctx.lineWidth = 2;
        ctx.lineJoin = 'round';
        ctx.lineCap = 'round';

        // Dispatch based on Mode
        if (waveformMode === 'trend') {
            drawTrendGraph(ctx, width, height);
            // Trend graph doesn't need high FPS, but keeping loop is simpler for switching
            // We could throttle this heavily
        } else {
            // ECG Mode
            drawEcgFrame(ctx, width, height, timestamp);
        }

        animationId = requestAnimationFrame(loop);
    }
    
    // --- Drawing Sub-functions ---

    function drawTrendGraph(ctx, width, height) {
        ctx.clearRect(0, 0, width, height);
        
        // Filter out zeros for cleaner graph if desired, but zeros show disconnect
        // Let's keep zeros but maybe handle scaling intelligently
        
        let min = Math.min(...hrHistory);
        let max = Math.max(...hrHistory);
        
        let range = max - min;
        const MIN_RANGE = 30;
        
        if (range < MIN_RANGE) {
            const mid = (max + min) / 2;
            if (mid === 0) {
                min = 0;
                max = MIN_RANGE;
            } else {
                min = mid - MIN_RANGE / 2;
                max = mid + MIN_RANGE / 2;
            }
            range = max - min;
        }
        
        const stepX = width / (hrHistory.length - 1);

        ctx.beginPath();
        
        let started = false;
        
        for (let i = 0; i < hrHistory.length; i++) {
            const val = hrHistory[i];
            const x = i * stepX;
            
            // Invert Y (canvas 0 is top)
            // Normalized 0..1
            const normalizedY = (val - min) / range;
            // Padding 10%
            const y = height - (normalizedY * height * 0.8 + height * 0.1); 
            
            if (i === 0) {
                ctx.moveTo(x, y);
            } else {
                // Smooth curve
                const prevX = (i - 1) * stepX;
                const prevVal = hrHistory[i-1];
                const prevNormY = (prevVal - min) / range;
                const prevY = height - (prevNormY * height * 0.8 + height * 0.1);
                
                const midX = (prevX + x) / 2;
                const midY = (prevY + y) / 2;
                
                ctx.quadraticCurveTo(prevX, prevY, midX, midY);
            }
        }
        // Connect last segment
        const lastI = hrHistory.length - 1;
        if (lastI > 0) {
             const val = hrHistory[lastI];
             const normalizedY = (val - min) / range;
             const y = height - (normalizedY * height * 0.8 + height * 0.1); 
             ctx.lineTo(width, y);
        }

        ctx.stroke();
    }

    function drawEcgFrame(ctx, width, height, timestamp) {
        // Throttle speed
        frameCount++;
        if (frameCount % speedDivider !== 0) {
             return; // Skip update, keep previous frame (actually we cleared it? No, wait)
             // If we return here, the canvas is NOT cleared because clearRect is inside loop but before this call?
             // Wait, I moved clearRect to loop.
             // If I return here, I must NOT clearRect in parent.
             // Refactoring: clearRect should be inside here or handle throttling differently.
        }
        
        // Actually, for scrolling effect, we MUST redraw every time we shift data.
        // If we throttle, we just don't shift data.
        // So we should NOT clearRect if we skip.
        
        // FIX: The parent loop logic needs adjustment for throttling ECG specifically.
        // Let's do shifting only on frame tick, but drawing always? 
        // No, if we don't shift, the image is static.
        
        // Correct Logic:
        // 1. Shift buffer (Throttle this)
        // 2. Clear & Draw (Always, or only when shifted)
        
        // Let's move buffer logic here.
        
        // Ensure buffer matches width
        if (ecgData.length !== width) {
            ecgData = new Array(width).fill(0);
        }

        // Logic to generate next pixel value
        let nextValue = 0; // Baseline

        if (isConnected && currentBpm > 0) {
            const intervalMs = 60000 / currentBpm;
            
            // Check if it's time for a new beat
            if (waveIndex === -1 && (timestamp - lastBeatTime > intervalMs)) {
                waveIndex = 0; // Start wave
                lastBeatTime = timestamp;
            }
        } else {
             waveIndex = -1;
        }

        // If currently drawing a wave
        if (waveIndex >= 0) {
            if (waveIndex < QRS_WAVE.length) {
                const scale = (height * 0.9) / 25; 
                nextValue += QRS_WAVE[waveIndex] * scale;
                waveIndex++;
            } else {
                waveIndex = -1; // Wave finished
            }
        }

        // Shift buffer and append new value
        ecgData.shift();
        ecgData.push(nextValue);

        // Draw
        ctx.clearRect(0, 0, width, height);
        
        ctx.beginPath();
        const centerY = height / 2;
        
        for (let i = 0; i < width; i++) {
            // Add slight random jitter for "analog" feel
            const jitter = (Math.random() - 0.5) * 1.5;
            const y = centerY + ecgData[i] + jitter;
            
            if (i === 0) ctx.moveTo(i, y);
            else ctx.lineTo(i, y);
        }
        ctx.stroke();
    }

    animationId = requestAnimationFrame(loop);
}

// Replaces old drawWaveform (now unused, but kept empty or removed to avoid errors if called)
function drawWaveform() {
    // No-op, handled by startEcgAnimation loop
}
