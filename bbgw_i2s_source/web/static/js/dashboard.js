/**
 * RPi I2S Audio Source Dashboard
 * 
 * Handles all client-side logic for the web UI:
 * - Server-Sent Events (SSE) for real-time status updates
 * - API calls for audio control (tone, sweep, WAV)
 * - Bluetooth control via UART commands
 * - UI updates and user interaction handling
 */

// Global state
let eventSource = null;
let currentSource = 'tone';
let isConnected = false;

// Initialize on page load
document.addEventListener('DOMContentLoaded', function() {
    initializeUI();
    connectSSE();
    attachEventListeners();
    updateStatus();  // Initial status fetch
});

/**
 * Initialize UI elements and default values
 */
function initializeUI() {
    // Set frequency slider to logarithmic scale display
    updateFrequencyDisplay();
    updateAmplitudeDisplay();
    updateDualFrequencyDisplay();
    
    // Show/hide controls based on source selection
    showSourceControls('tone');
}

/**
 * Connect to Server-Sent Events stream for real-time updates
 */
function connectSSE() {
    if (eventSource) {
        eventSource.close();
    }
    
    // Update connection status
    updateConnectionStatus('connecting');
    
    eventSource = new EventSource('/api/stream');
    
    eventSource.onopen = function() {
        console.log('SSE connection established');
        isConnected = true;
        updateConnectionStatus('connected');
    };
    
    eventSource.onmessage = function(event) {
        try {
            const data = JSON.parse(event.data);
            updateDashboard(data);
        } catch (e) {
            console.error('Error parsing SSE data:', e);
        }
    };
    
    eventSource.onerror = function(error) {
        console.error('SSE error:', error);
        isConnected = false;
        updateConnectionStatus('disconnected');
        
        // Attempt reconnection after 5 seconds
        setTimeout(connectSSE, 5000);
    };
}

/**
 * Fallback: Fetch status via polling (if SSE fails)
 */
function updateStatus() {
    fetch('/api/status')
        .then(response => response.json())
        .then(data => updateDashboard(data))
        .catch(error => console.error('Status fetch error:', error));
}

/**
 * Update dashboard with status data
 */
function updateDashboard(data) {
    // Update I2S status
    if (data.i2s) {
        document.getElementById('statusI2S').textContent = data.i2s.active ? 'Active' : 'Stopped';
        document.getElementById('statusI2S').className = `badge ${data.i2s.active ? 'bg-success' : 'bg-secondary'}`;
        
        document.getElementById('statFramesSent').textContent = formatNumber(data.i2s.frames_sent || 0);
        document.getElementById('statUnderruns').textContent = data.i2s.underruns || 0;
        
        const bufferFill = data.i2s.buffer_fill_pct || 0;
        updateBufferFill(bufferFill);
    }
    
    // Update audio status
    if (data.audio) {
        document.getElementById('statusAudioSource').textContent = data.audio.source || 'Unknown';
        document.getElementById('statusAudioSource').className = 'badge bg-primary';
        
        // Update audio info panel
        updateAudioInfo(data.audio);
    }
    
    // Update Bluetooth status
    if (data.bt) {
        const btStatus = data.bt.connected ? 'Connected' : 'Disconnected';
        document.getElementById('statusBluetooth').textContent = btStatus;
        document.getElementById('statusBluetooth').className = `badge ${data.bt.connected ? 'bg-success' : 'bg-secondary'}`;
    }
    
    // Update system info
    if (data.system) {
        document.getElementById('statusCpuTemp').textContent = data.system.cpu_temp 
            ? `${data.system.cpu_temp.toFixed(1)}°C` 
            : '--°C';
        document.getElementById('statusMemory').textContent = data.system.memory_mb 
            ? `${data.system.memory_mb.toFixed(0)} MB` 
            : '-- MB';
        document.getElementById('statusUptime').textContent = formatUptime(data.system.uptime_seconds || 0);
    }
}

/**
 * Update audio info panel based on current source
 */
function updateAudioInfo(audioData) {
    // Hide all audio info sections
    document.getElementById('audioInfoTone').style.display = 'none';
    document.getElementById('audioInfoAmplitude').style.display = 'none';
    document.getElementById('audioInfoMode').style.display = 'none';
    document.getElementById('audioInfoWav').style.display = 'none';
    
    if (audioData.source === 'tone') {
        document.getElementById('audioInfoTone').style.display = 'block';
        document.getElementById('audioInfoAmplitude').style.display = 'block';
        document.getElementById('audioInfoMode').style.display = 'block';
        
        document.getElementById('audioFreq').textContent = `${audioData.tone_freq || 0} Hz`;
        document.getElementById('audioAmplitude').textContent = `${Math.round((audioData.tone_amp || 0) * 100)}%`;
        document.getElementById('audioMode').textContent = audioData.tone_mode || 'mono';
    } else if (audioData.source === 'wav') {
        document.getElementById('audioInfoWav').style.display = 'block';
        document.getElementById('audioWavFile').textContent = audioData.wav_file || '--';
    }
}

/**
 * Update buffer fill progress bar
 */
function updateBufferFill(percentage) {
    const bar = document.getElementById('bufferFillBar');
    const text = document.getElementById('bufferFillText');
    
    bar.style.width = `${percentage}%`;
    bar.setAttribute('aria-valuenow', percentage);
    text.textContent = `${percentage.toFixed(0)}%`;
    
    // Color code based on fill level
    bar.className = 'progress-bar';
    if (percentage >= 50) {
        bar.classList.add('bg-success');
    } else if (percentage >= 25) {
        bar.classList.add('bg-warning');
    } else {
        bar.classList.add('bg-danger');
    }
}

/**
 * Update connection status indicator
 */
function updateConnectionStatus(status) {
    const indicator = document.getElementById('status-indicator');
    const text = document.getElementById('status-text');
    
    indicator.className = 'bi bi-circle-fill ' + status;
    
    if (status === 'connected') {
        text.textContent = 'Connected';
    } else if (status === 'connecting') {
        text.textContent = 'Connecting...';
    } else {
        text.textContent = 'Disconnected';
    }
}

/**
 * Attach event listeners to UI controls
 */
function attachEventListeners() {
    // Audio source selection
    document.querySelectorAll('input[name="audioSource"]').forEach(radio => {
        radio.addEventListener('change', function() {
            currentSource = this.value;
            showSourceControls(this.value);
            
            if (this.value === 'silence') {
                applySilence();
            }
        });
    });
    
    // Tone controls
    document.getElementById('toneFreq').addEventListener('input', updateFrequencyDisplay);
    document.getElementById('toneAmp').addEventListener('input', updateAmplitudeDisplay);
    document.getElementById('dualFreq').addEventListener('input', updateDualFrequencyDisplay);
    document.getElementById('toneMode').addEventListener('change', function() {
        const dualControls = document.getElementById('dualToneControls');
        dualControls.style.display = this.value === 'dual' ? 'block' : 'none';
    });
    document.getElementById('btnApplyTone').addEventListener('click', applyToneSettings);
    
    // Sweep controls
    document.getElementById('btnStartSweep').addEventListener('click', startSweep);
    
    // WAV controls
    document.getElementById('btnPlayWav').addEventListener('click', playWav);
    
    // Bluetooth controls
    document.getElementById('btnBtScan').addEventListener('click', bluetoothScan);
    document.getElementById('btnBtConnect').addEventListener('click', bluetoothConnect);
    document.getElementById('btnBtDisconnect').addEventListener('click', bluetoothDisconnect);
    document.getElementById('btnBtStart').addEventListener('click', bluetoothStart);
    document.getElementById('btnBtStop').addEventListener('click', bluetoothStop);
}

/**
 * Show/hide controls based on selected audio source
 */
function showSourceControls(source) {
    document.getElementById('toneControls').style.display = source === 'tone' ? 'block' : 'none';
    document.getElementById('sweepControls').style.display = source === 'sweep' ? 'block' : 'none';
    document.getElementById('wavControls').style.display = source === 'wav' ? 'block' : 'none';
}

/**
 * Update frequency display (logarithmic scale)
 */
function updateFrequencyDisplay() {
    const slider = document.getElementById('toneFreq');
    const value = parseInt(slider.value);
    document.getElementById('toneFreqValue').textContent = formatNumber(value);
}

/**
 * Update amplitude display
 */
function updateAmplitudeDisplay() {
    const slider = document.getElementById('toneAmp');
    document.getElementById('toneAmpValue').textContent = slider.value;
}

/**
 * Update dual-tone frequency display
 */
function updateDualFrequencyDisplay() {
    const slider = document.getElementById('dualFreq');
    const value = parseInt(slider.value);
    document.getElementById('dualFreqValue').textContent = formatNumber(value);
}

/**
 * Apply tone settings to audio engine
 */
function applyToneSettings() {
    const freq = parseInt(document.getElementById('toneFreq').value);
    const amp = parseInt(document.getElementById('toneAmp').value) / 100.0;
    const mode = document.getElementById('toneMode').value;
    
    const payload = {
        freq: freq,
        amp: amp,
        mode: mode
    };
    
    // Add dual frequency if in dual mode
    if (mode === 'dual') {
        payload.dual_freq = parseInt(document.getElementById('dualFreq').value);
    }
    
    fetch('/api/tone', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(payload)
    })
    .then(response => response.json())
    .then(data => {
        if (data.status === 'ok') {
            showAlert('Tone settings applied successfully', 'success');
        } else {
            showAlert(`Error: ${data.message}`, 'danger');
        }
    })
    .catch(error => {
        showAlert(`Error: ${error.message}`, 'danger');
    });
}

/**
 * Start frequency sweep
 */
function startSweep() {
    const duration = parseInt(document.getElementById('sweepDuration').value);
    const loop = document.getElementById('sweepLoop').checked;
    
    fetch('/api/sweep', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify({
            duration: duration,
            loop: loop
        })
    })
    .then(response => response.json())
    .then(data => {
        if (data.status === 'ok') {
            showAlert(`Sweep started: 20 Hz → 20 kHz over ${duration}s`, 'success');
        } else {
            showAlert(`Error: ${data.message}`, 'danger');
        }
    })
    .catch(error => {
        showAlert(`Error: ${error.message}`, 'danger');
    });
}

/**
 * Play WAV file
 */
function playWav() {
    const file = document.getElementById('wavFile').value.trim();
    const loop = document.getElementById('wavLoop').checked;
    
    if (!file) {
        showAlert('Please enter a WAV filename', 'warning');
        return;
    }
    
    fetch('/api/wav', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify({
            file: file,
            loop: loop
        })
    })
    .then(response => response.json())
    .then(data => {
        if (data.status === 'ok') {
            showAlert(`Playing WAV file: ${file}`, 'success');
        } else {
            showAlert(`Error: ${data.message}`, 'danger');
        }
    })
    .catch(error => {
        showAlert(`Error: ${error.message}`, 'danger');
    });
}

/**
 * Apply silence mode
 */
function applySilence() {
    fetch('/api/silence', {
        method: 'POST'
    })
    .then(response => response.json())
    .then(data => {
        if (data.status === 'ok') {
            showAlert('Silence mode activated', 'success');
        } else {
            showAlert(`Error: ${data.message}`, 'danger');
        }
    })
    .catch(error => {
        showAlert(`Error: ${error.message}`, 'danger');
    });
}

/**
 * Bluetooth scan for devices
 */
function bluetoothScan() {
    const btn = document.getElementById('btnBtScan');
    btn.disabled = true;
    btn.innerHTML = '<span class="spinner-border spinner-border-sm"></span> Scanning...';
    
    sendBluetoothCommand('SCAN', '')
        .then(data => {
            btn.disabled = false;
            btn.innerHTML = '<i class="bi bi-search"></i> Scan';
            
            if (data.status === 'ok') {
                const deviceCount = parseInt(data.result) || 0;
                showAlert(`Scan complete: Found ${deviceCount} device(s)`, 'success');
                
                // TODO: Fetch and display device list
                // For now, just show success
            } else {
                showAlert(`Scan error: ${data.message}`, 'danger');
            }
        })
        .catch(error => {
            btn.disabled = false;
            btn.innerHTML = '<i class="bi bi-search"></i> Scan';
            showAlert(`Scan error: ${error.message}`, 'danger');
        });
}

/**
 * Connect to Bluetooth device
 */
function bluetoothConnect() {
    const mac = document.getElementById('btMacAddress').value.trim();
    
    if (!mac) {
        showAlert('Please enter a MAC address', 'warning');
        return;
    }
    
    const btn = document.getElementById('btnBtConnect');
    btn.disabled = true;
    btn.innerHTML = '<span class="spinner-border spinner-border-sm"></span> Connecting...';
    
    sendBluetoothCommand('CONNECT', mac)
        .then(data => {
            btn.disabled = false;
            btn.innerHTML = '<i class="bi bi-link-45deg"></i> Connect';
            
            if (data.status === 'ok') {
                showAlert(`Connected to ${mac}`, 'success');
            } else {
                showAlert(`Connection error: ${data.message}`, 'danger');
            }
        })
        .catch(error => {
            btn.disabled = false;
            btn.innerHTML = '<i class="bi bi-link-45deg"></i> Connect';
            showAlert(`Connection error: ${error.message}`, 'danger');
        });
}

/**
 * Disconnect Bluetooth
 */
function bluetoothDisconnect() {
    sendBluetoothCommand('DISCONNECT', '')
        .then(data => {
            if (data.status === 'ok') {
                showAlert('Bluetooth disconnected', 'success');
            } else {
                showAlert(`Disconnect error: ${data.message}`, 'danger');
            }
        })
        .catch(error => {
            showAlert(`Disconnect error: ${error.message}`, 'danger');
        });
}

/**
 * Start Bluetooth playback
 */
function bluetoothStart() {
    sendBluetoothCommand('START', '')
        .then(data => {
            if (data.status === 'ok') {
                showAlert('Bluetooth playback started', 'success');
            } else {
                showAlert(`Start error: ${data.message}`, 'danger');
            }
        })
        .catch(error => {
            showAlert(`Start error: ${error.message}`, 'danger');
        });
}

/**
 * Stop Bluetooth playback
 */
function bluetoothStop() {
    sendBluetoothCommand('STOP', '')
        .then(data => {
            if (data.status === 'ok') {
                showAlert('Bluetooth playback stopped', 'success');
            } else {
                showAlert(`Stop error: ${data.message}`, 'danger');
            }
        })
        .catch(error => {
            showAlert(`Stop error: ${error.message}`, 'danger');
        });
}

/**
 * Send Bluetooth command via UART
 */
function sendBluetoothCommand(command, args) {
    return fetch('/api/bt/command', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify({
            command: command,
            args: args
        })
    })
    .then(response => {
        if (response.status === 503) {
            throw new Error('Bluetooth UART not available');
        }
        return response.json();
    });
}

/**
 * Show alert message
 */
function showAlert(message, type) {
    const alertArea = document.getElementById('alertArea');
    const alertId = 'alert-' + Date.now();
    
    const alertHtml = `
        <div class="alert alert-${type} alert-dismissible fade show" role="alert" id="${alertId}">
            ${message}
            <button type="button" class="btn-close" data-bs-dismiss="alert"></button>
        </div>
    `;
    
    alertArea.insertAdjacentHTML('beforeend', alertHtml);
    
    // Auto-dismiss after 5 seconds
    setTimeout(() => {
        const alertEl = document.getElementById(alertId);
        if (alertEl) {
            const bsAlert = new bootstrap.Alert(alertEl);
            bsAlert.close();
        }
    }, 5000);
}

/**
 * Format number with thousands separator
 */
function formatNumber(num) {
    return num.toString().replace(/\B(?=(\d{3})+(?!\d))/g, ',');
}

/**
 * Format uptime seconds to human-readable format
 */
function formatUptime(seconds) {
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    const secs = Math.floor(seconds % 60);
    
    if (hours > 0) {
        return `${hours}h ${minutes}m`;
    } else if (minutes > 0) {
        return `${minutes}m ${secs}s`;
    } else {
        return `${secs}s`;
    }
}
