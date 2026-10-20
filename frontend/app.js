/**
 * SirenLink App Logic — Single-Page (index.html) views
 * Existing UI preserved. WebSocket is now real.
 */

const app = {
    state: {
        role: null,
        id: null,
        token: null,
        ws: null,
        maps: {},
        markers: {},
        isEmergencyActive: false,
        timerInterval: null,
        startTime: null,
        audioContext: null,
        oscillators: [],
        soundMuted: false,
        currentAlertId: null,
    },

    init() {
        this.setupRouting();
        this.setupAuthModal();
        this.checkAuth();
        this.handleRoute();
    },

    setupRouting() {
        window.addEventListener('hashchange', () => this.handleRoute());
    },

    handleRoute() {
        const hash = window.location.hash || '#landing';
        document.querySelectorAll('.view').forEach(el => el.classList.add('hidden'));
        const view = document.querySelector(hash);
        if (view) {
            view.classList.remove('hidden');
            if (['#hospital', '#ambulance', '#vehicle'].includes(hash)) {
                if (!this.state.token) {
                    window.location.hash = '#landing';
                    this.showLoginModal(hash.replace('#', ''));
                    return;
                }
                this.initMap(hash.replace('#', ''));
                this.connectWebSocket();
            }
        }
    },

    showLoginModal(role = 'hospital') {
        document.getElementById('login-modal').classList.remove('hidden');
        this.switchTab(role);
    },

    hideLoginModal() {
        document.getElementById('login-modal').classList.add('hidden');
    },

    setupAuthModal() {
        document.querySelectorAll('.tab').forEach(tab => {
            tab.addEventListener('click', (e) => {
                this.switchTab(e.target.dataset.role);
            });
        });

        document.getElementById('login-form').addEventListener('submit', (e) => {
            e.preventDefault();
            const id = document.getElementById('login-id').value;
            this.login(this.currentTab, id);
        });
    },

    switchTab(role) {
        this.currentTab = role;
        document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
        document.querySelector(`.tab[data-role="${role}"]`).classList.add('active');
        
        const passGroup = document.getElementById('login-pass-group');
        const idLabel = document.querySelector('label[for="login-id"]');
        
        if (role === 'hospital') {
            passGroup.classList.remove('hidden');
            if (idLabel) idLabel.textContent = 'Hospital Name / ID';
        } else if (role === 'ambulance') {
            passGroup.classList.add('hidden');
            if (idLabel) idLabel.textContent = 'Vehicle Number (Ambulance)';
        } else {
            passGroup.classList.add('hidden');
            if (idLabel) idLabel.textContent = 'Vehicle Number';
        }
    },

    login(role, id) {
        this.state.role = role;
        this.state.id = id;
        this.state.token = 'mock-token-' + Date.now();
        
        sessionStorage.setItem('sl_token', this.state.token);
        sessionStorage.setItem('sl_role', role);
        sessionStorage.setItem('sl_id', id);

        this.hideLoginModal();
        this.updateUserLabels();
        window.location.hash = `#${role}`;
        this.connectWebSocket();
    },

    logout() {
        sessionStorage.clear();
        this.state.token = null;
        this.state.role = null;
        if (this.state.ws) this.state.ws.close();
        window.location.hash = '#landing';
    },

    checkAuth() {
        const token = sessionStorage.getItem('sl_token');
        if (token) {
            this.state.token = token;
            this.state.role = sessionStorage.getItem('sl_role');
            this.state.id = sessionStorage.getItem('sl_id');
            this.updateUserLabels();
        }
    },

    updateUserLabels() {
        if (this.state.role === 'hospital') document.getElementById('h-hosp-name').textContent = this.state.id;
        if (this.state.role === 'ambulance') document.getElementById('a-veh-num').textContent = this.state.id;
        if (this.state.role === 'vehicle') {
            const display = document.getElementById('v-reg-num-display');
            if (display) display.textContent = this.state.id;
        }
    },

    initMap(role) {
        const mapId = `${role}Map`;
        const mapContainer = document.getElementById(mapId);
        
        if (!mapContainer) return;

        if (this.state.maps[mapId]) {
            this.state.maps[mapId].invalidateSize();
            return;
        }

        const map = L.map(mapId).setView([19.0760, 72.8777], 14);
        L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
            attribution: '© OpenStreetMap contributors'
        }).addTo(map);

        this.state.maps[mapId] = map;

        if (role === 'hospital' || role === 'ambulance') {
            this.state.markers.hosp = L.marker([19.0760, 72.8777]).addTo(map).bindPopup("General Hospital");
        }
        
        setTimeout(() => map.invalidateSize(), 200);
    },

    // ── Real WebSocket Connection ──────────────────────────────
    connectWebSocket() {
        if (!this.state.role || !this.state.id) return;
        if (this.state.ws && this.state.ws.readyState === WebSocket.OPEN) return;

        const wsProtocol = window.location.protocol === 'https:' ? 'wss' : 'ws';
        const wsUrl = `${wsProtocol}://${window.location.host}/ws/${this.state.role}/${this.state.id}`;
        try {
            this.state.ws = new WebSocket(wsUrl);
        } catch(e) {
            console.warn('WebSocket connect failed:', e);
            return;
        }

        this.state.ws.onopen = () => {
            console.log(`[SirenLink] WebSocket connected as ${this.state.role}/${this.state.id}`);
            this.updateConnectionStatus(true);
        };

        this.state.ws.onclose = () => {
            this.updateConnectionStatus(false);
            // Reconnect after 3 seconds
            setTimeout(() => this.connectWebSocket(), 3000);
        };

        this.state.ws.onerror = () => {
            this.updateConnectionStatus(false);
        };

        this.state.ws.onmessage = (event) => {
            try {
                const msg = JSON.parse(event.data);
                this.handleWebSocketMessage(msg);
            } catch(e) {
                console.warn('WS parse error:', e);
            }
        };
    },

    updateConnectionStatus(connected) {
        // Update connection dots in vehicle dashboard
        const dots = document.querySelectorAll('.status-dot');
        dots.forEach(dot => {
            dot.style.background = connected ? 'var(--success)' : 'var(--danger)';
            dot.style.boxShadow = connected
                ? '0 0 8px var(--success)'
                : '0 0 8px var(--danger)';
        });
        const connStatus = document.querySelector('.v-connection-status');
        if (connStatus) {
            connStatus.innerHTML = `<span class="status-dot"></span> ${connected ? 'CONNECTED' : 'DISCONNECTED'}`;
            connStatus.style.color = connected ? 'var(--success)' : 'var(--danger)';
        }
    },

    handleWebSocketMessage(msg) {
        const t = msg.type;

        // Handle all incoming messages based on current role
        if (this.state.role === 'vehicle') {
            this.handleVehicleMessage(msg);
        } else if (this.state.role === 'hospital') {
            this.handleHospitalMessage(msg);
        } else if (this.state.role === 'ambulance') {
            this.handleAmbulanceMessage(msg);
        }
    },

    handleVehicleMessage(msg) {
        const t = msg.type;
        if (t === 'emergency_activated' || t === 'state_update') {
            if (msg.vehicle_instruction && msg.vehicle_instruction !== 'STANDBY') {
                const dist = msg.distance_to_vehicle;
                const instr = msg.vehicle_instruction;
                const dirMap = {
                    'AMBULANCE_DETECTED':  '🟡 AMBULANCE DETECTED',
                    'PREPARE_TO_GIVE_WAY': '🟠 PREPARE TO GIVE WAY',
                    'MOVE_LEFT':           '⬅️ MOVE LEFT',
                    'MOVE_RIGHT':          '➡️ MOVE RIGHT',
                    'AMBULANCE_PASSING':   '🚨 AMBULANCE PASSING',
                };
                this.showVehicleAlert(
                    msg.ambulance_id || 'AMB-A01',
                    dist ? `${Math.round(dist)} m` : '--',
                    dirMap[instr] || instr,
                    `Ambulance: ${instr.replace(/_/g,' ')}`
                );
            }
        }
        if (t === 'emergency_deactivated' || t === 'ambulance_arrived') {
            this.clearVehicleAlert();
            if (t === 'ambulance_arrived') {
                this.addAlertHistory('✅ Road Cleared', 'AMBULANCE ARRIVED', true);
            }
        }
    },

    handleHospitalMessage(msg) {
        if (msg.type === 'emergency_activated') {
            document.getElementById('h-stat-active').textContent = '1';
            document.getElementById('h-stat-enroute').textContent = '1';
        }
        if (msg.vehicles_alerted !== undefined) {
            document.getElementById('h-stat-vehicles').textContent = msg.vehicles_alerted;
        }
        if (msg.type === 'state_update' || msg.type === 'emergency_activated') {
            // Update ambulance list
            const dist = msg.distance_to_hospital;
            const eta  = msg.eta_seconds;
            const li = document.createElement('li');
            li.style.cssText = 'padding:0.75rem;border-bottom:1px solid var(--border-glass);font-size:0.85rem;';
            li.innerHTML = `
                <strong>${msg.ambulance_id || 'AMB-A01'}</strong>
                ${dist ? ` — ${dist >= 1000 ? (dist/1000).toFixed(1)+'km' : Math.round(dist)+'m'}` : ''}
                ${eta ? ` | ETA: ${Math.floor(eta/60)}m ${Math.round(eta%60)}s` : ''}
            `;
            const list = document.getElementById('h-amb-list');
            list.innerHTML = '';
            list.appendChild(li);
        }
        if (msg.type === 'ambulance_arrived') {
            const li = document.createElement('li');
            li.style.cssText = 'padding:0.75rem;color:var(--success);font-weight:700;';
            li.textContent = '✅ AMBULANCE ARRIVED';
            const list = document.getElementById('h-amb-list');
            list.innerHTML = '';
            list.appendChild(li);
        }
    },

    handleAmbulanceMessage(msg) {
        if (msg.type === 'state_update' && msg.vehicles_alerted !== undefined) {
            document.getElementById('a-alerted').textContent = msg.vehicles_alerted;
        }
    },

    showVehicleAlert(ambId, distance, direction, reason) {
        const card = document.getElementById('v-current-emergency');
        card.classList.remove('idle');
        card.classList.add('active');
        card.querySelector('.v-em-idle-content').classList.add('hidden');
        card.querySelector('.v-em-active-content').classList.remove('hidden');
        
        document.getElementById('v-direction').textContent = direction;
        document.getElementById('v-em-reason').textContent = reason;
        document.getElementById('v-info-id').textContent = ambId;
        document.getElementById('v-info-dist').textContent = distance;
        
        this.addAlertHistory(`🚨 ${ambId}`, direction, false);
        if (!this.state.soundMuted) this.startSiren();
    },

    clearVehicleAlert() {
        const card = document.getElementById('v-current-emergency');
        card.classList.add('idle');
        card.classList.remove('active');
        card.querySelector('.v-em-idle-content').classList.remove('hidden');
        card.querySelector('.v-em-active-content').classList.add('hidden');
        this.stopAudio();
    },

    // ── Web Audio API ────────────────────────────────────────
    initAudio() {
        if (!this.state.audioContext) {
            this.state.audioContext = new (window.AudioContext || window.webkitAudioContext)();
        }
        if (this.state.audioContext.state === 'suspended') {
            this.state.audioContext.resume();
        }
    },

    startSiren() {
        this.initAudio();
        this.stopAudio();
        
        const osc = this.state.audioContext.createOscillator();
        const gainNode = this.state.audioContext.createGain();
        
        osc.type = 'square';
        osc.frequency.setValueAtTime(600, this.state.audioContext.currentTime);
        const sirenInterval = setInterval(() => {
            if (osc && osc.frequency) {
                osc.frequency.value = osc.frequency.value === 600 ? 800 : 600;
            }
        }, 500);
        osc._sirenInterval = sirenInterval;

        gainNode.gain.value = 0.08;
        osc.connect(gainNode);
        gainNode.connect(this.state.audioContext.destination);
        osc.start();
        this.state.oscillators.push(osc);
    },

    startBuzzer() {
        this.initAudio();
        this.stopAudio();
        const osc = this.state.audioContext.createOscillator();
        osc.type = 'sawtooth';
        osc.frequency.value = 1000;
        osc.connect(this.state.audioContext.destination);
        osc.start();
        setTimeout(() => { try { osc.stop(); } catch(e){} }, 200);
    },

    stopAudio() {
        this.state.oscillators.forEach(osc => {
            try {
                if (osc._sirenInterval) clearInterval(osc._sirenInterval);
                osc.stop();
            } catch(e){}
        });
        this.state.oscillators = [];
    },

    // ── Ambulance Logic ───────────────────────────────────────
    toggleEmergency() {
        this.startBuzzer();
        const btn = document.getElementById('btn-siren');
        const stats = document.getElementById('a-em-stats');
        
        this.state.isEmergencyActive = !this.state.isEmergencyActive;
        
        if (this.state.isEmergencyActive) {
            btn.classList.remove('idle');
            btn.classList.add('active');
            btn.innerHTML = '<span class="siren-icon">🛑</span><span class="siren-text">EMERGENCY ACTIVE</span>';
            stats.classList.remove('hidden');
            
            this.state.startTime = Date.now();
            this.state.timerInterval = setInterval(() => {
                const diff = Math.floor((Date.now() - this.state.startTime) / 1000);
                const h = String(Math.floor(diff / 3600)).padStart(2, '0');
                const m = String(Math.floor((diff % 3600) / 60)).padStart(2, '0');
                const s = String(diff % 60).padStart(2, '0');
                document.getElementById('a-timer').textContent = `${h}:${m}:${s}`;
            }, 1000);
            
            // Send real emergency activate to backend via WS
            if (this.state.ws && this.state.ws.readyState === WebSocket.OPEN) {
                this.state.ws.send(JSON.stringify({
                    type: 'emergency_activate',
                    ambulance_id: this.state.id,
                }));
            }
        } else {
            btn.classList.add('idle');
            btn.classList.remove('active');
            btn.innerHTML = '<span class="siren-icon">🚨</span><span class="siren-text">ACTIVATE EMERGENCY</span>';
            stats.classList.add('hidden');
            clearInterval(this.state.timerInterval);
            
            if (this.state.ws && this.state.ws.readyState === WebSocket.OPEN) {
                this.state.ws.send(JSON.stringify({
                    type: 'emergency_deactivate',
                    ambulance_id: this.state.id,
                }));
            }
        }
    },

    simulateAmbulance() {
        if (!this.state.isEmergencyActive) {
            alert("Activate emergency first!");
            return;
        }
        document.getElementById('a-alerted').textContent = "5";
        document.getElementById('a-acks').textContent = "2";
    },

    // ── Vehicle Logic ─────────────────────────────────────────
    simulateVehicleAlert() {
        this.showVehicleAlert('AMB-001', '150 m', '⬅️ MOVE LEFT', 'Ambulance approaching from your rear-right.');
    },

    acknowledgeAlert() {
        this.clearVehicleAlert();
        document.getElementById('v-info-id').textContent = '--';
        document.getElementById('v-info-dist').textContent = '--';
        document.getElementById('v-info-speed').textContent = '--';
        document.getElementById('v-info-dir').textContent = '--';
        document.getElementById('v-info-hosp').textContent = '--';
        document.getElementById('v-info-eta').textContent = '--';
        
        const lastItem = document.querySelector('#v-history-list li:first-child');
        if (lastItem && !lastItem.classList.contains('history-empty')) {
            lastItem.innerHTML = `
                <div>
                    <div class="history-item-title">✓ Emergency cleared</div>
                    <div class="history-item-time">${new Date().toLocaleTimeString()}</div>
                </div>
                <div class="history-item-ack">✓</div>
            `;
        }
        if (this.state.ws && this.state.ws.readyState === WebSocket.OPEN) {
            this.state.ws.send(JSON.stringify({ type: 'road_cleared' }));
        }
    },
    
    addAlertHistory(title, reason, acked) {
        const list = document.getElementById('v-history-list');
        const empty = list.querySelector('.history-empty');
        if (empty) empty.remove();
        
        const li = document.createElement('li');
        li.innerHTML = `
            <div>
                <div class="history-item-title">${title}</div>
                <div class="history-item-time">${new Date().toLocaleTimeString()}</div>
            </div>
            ${acked ? '<div class="history-item-ack">✓</div>' : ''}
        `;
        list.prepend(li);
    },
    
    toggleSound() {
        this.state.soundMuted = !this.state.soundMuted;
        const btn = document.getElementById('btn-sound-toggle');
        if (this.state.soundMuted) {
            btn.textContent = '🔈 Sound OFF';
            this.stopAudio();
        } else {
            btn.textContent = '🔊 Sound ON';
        }
    }
};

// Initialize app when DOM loads
document.addEventListener('DOMContentLoaded', () => app.init());
