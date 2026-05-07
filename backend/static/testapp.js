const ctx = document.getElementById('liveChart').getContext('2d');

const gradient = ctx.createLinearGradient(0, 0, 0, 300);
gradient.addColorStop(0, 'rgba(140, 122, 242, 0.5)');
gradient.addColorStop(1, 'rgba(140, 122, 242, 0.0)');

const chartConfig = {
    type: 'line',
    data: {
        labels: [],
        datasets: [{
            label: 'Skin Conductance (µS)',
            data: [],
            borderColor: '#8c7af2',
            backgroundColor: gradient,
            borderWidth: 2,
            pointRadius: 0,
            fill: true,
            tension: 0.4
        }]
    },
    options: {
        responsive: true,
        maintainAspectRatio: false,
        plugins: {
            legend: { display: false },
            tooltip: {
                mode: 'index',
                intersect: false,
            }
        },
        scales: {
            x: {
                display: false, // hide x axis for a clean look
            },
            y: {
                display: true,
                title: {
                    display: true,
                    text: 'Conductance (µS)'
                },
                grid: {
                    color: '#edf2f7'
                }
            }
        },
        animation: {
            duration: 0
        }
    }
};

const liveChart = new Chart(ctx, chartConfig);

const tempCtx = document.getElementById('tempChart').getContext('2d');
const tempGradient = tempCtx.createLinearGradient(0, 0, 0, 250);
tempGradient.addColorStop(0, 'rgba(247, 160, 114, 0.5)');
tempGradient.addColorStop(1, 'rgba(247, 160, 114, 0.0)');

const tempChartConfig = {
    type: 'line',
    data: {
        labels: [],
        datasets: [{
            label: 'Temperature (°C)',
            data: [],
            borderColor: '#f7a072',
            backgroundColor: tempGradient,
            borderWidth: 2,
            pointRadius: 0,
            fill: true,
            tension: 0.4
        }]
    },
    options: {
        responsive: true,
        maintainAspectRatio: false,
        plugins: { legend: { display: false } },
        scales: {
            x: { display: false },
            y: {
                display: true,
                title: { display: true, text: 'Temp (°C)' },
                grid: { color: '#edf2f7' }
            }
        },
        animation: { duration: 0 }
    }
};
const tempChart = new Chart(tempCtx, tempChartConfig);

let baseline = 0.59;
const MAX_DATAPOINTS = 100;
let hotFlashCount = 0;
let lastDetectedState = false;

async function fetchData() {
    try {
        const response = await fetch('/api/data');
        const data = await response.json();
        
        if (data.history) {
            updateDashboard(data.history, data.inference, data.lastSync, data.deviceConnected, data.profileName, data.eventCount, data.insights);
        }
    } catch (error) {
        console.error("Error fetching data:", error);
        document.getElementById('nav-connection-status').textContent = 'Backend Offline';
        document.getElementById('nav-connection-status').previousElementSibling.className = 'status-dot offline';
    }
}

function updateDashboard(history, inference, lastSync, deviceConnected, profileName, eventCount, insights) {
    const statusEl = document.getElementById('nav-connection-status');
    const statusDot = statusEl.previousElementSibling;
    const heroConductance = document.getElementById('hero-conductance');
    const heroTrend = document.getElementById('hero-conductance-trend');

    if (!history || history.length === 0) {
        statusEl.textContent = 'Waiting for ESP data...';
        statusDot.className = 'status-dot offline';
        heroConductance.textContent = '--';
        heroTrend.textContent = 'Awaiting Sensor...';
        return;
    }

    const latest = history[history.length - 1];
    // Dynamic greeting based on time
    const greetingEl = document.getElementById("greeting");
    const hour = new Date().getHours();

    let greetingText = "Good Morning";
    if (hour >= 12 && hour < 18) greetingText = "Good Afternoon";
    if (hour >= 18) greetingText = "Good Evening";

    const name = profileName || "Gutruf";

    if (greetingEl) {
        greetingEl.innerHTML = `${greetingText}, <span id="profile-name">${name}</span>`;
    }
    
    // Update patient/device name if backend provides one
    const nameEl = document.getElementById('profile-name') 
                || document.getElementById('user-name') 
                || document.getElementById('device-name');
    
    if (nameEl && profileName) {
        nameEl.textContent = profileName;
    }

    // Update hot flash count
    const isDetected = inference && inference.state && inference.state.includes("Detected");

    if (isDetected && !lastDetectedState) {
        hotFlashCount += 1;
    }

    lastDetectedState = isDetected;

    const hotFlashCountEl = document.getElementById('hot-flash-count') 
         || document.getElementById('event-count');

    if (hotFlashCountEl) {
        hotFlashCountEl.textContent = hotFlashCount;
    }

    const trendEl = document.getElementById('hot-flash-trend');

    if (trendEl) {
        if (hotFlashCount === 0) {
            trendEl.textContent = "No events today";
            trendEl.className = "trend neutral";
        } else if (hotFlashCount < 3) {
            trendEl.innerHTML = '<i class="fa-solid fa-arrow-down"></i> Low frequency';
            trendEl.className = "trend positive";
        } else if (hotFlashCount < 6) {
            trendEl.textContent = "Moderate frequency";
            trendEl.className = "trend neutral";
        } else {
            trendEl.innerHTML = '<i class="fa-solid fa-arrow-up"></i> High frequency';
            trendEl.className = "trend text-red";
        }
    }
    
    // Update Hero
    heroConductance.textContent = latest.edaAvailable ? latest.conductance.toFixed(2) : '--';
    document.getElementById('hero-temp').textContent = latest.temperature.toFixed(1);
    document.getElementById('hero-humidity').textContent = latest.humidity.toFixed(0);

    // Update Connection Status
    if (deviceConnected) {
        statusEl.textContent = latest.edaAvailable ? 'ESP Connected' : 'Temp-only Mode';
        statusDot.className = 'status-dot online';
    } else {
        statusEl.textContent = 'ESP Disconnected';
        statusDot.className = 'status-dot offline';
    }

    // Update Battery Status (Assuming 4.2 max, 3.3 min)
    let batteryPct = ((latest.batteryVoltage - 3.3) / (4.2 - 3.3)) * 100;
    batteryPct = Math.min(100, Math.max(0, batteryPct));
    document.getElementById('nav-battery-status').textContent = `${batteryPct.toFixed(0)}% - ${latest.sensorStatus}`;
    
    // Update Charts
    const recentHistory = history.slice(-MAX_DATAPOINTS);
    const timeLabels = recentHistory.map(d => new Date(d.timestamp * 1000).toLocaleTimeString([], {hour: '2-digit', minute:'2-digit', second:'2-digit'}));
    
    // EDA Chart
    liveChart.data.labels = timeLabels;
    liveChart.data.datasets[0].data = recentHistory.map(d => d.edaAvailable ? d.conductance : null);
    liveChart.update();

    // Temp Chart
    tempChart.data.labels = timeLabels;
    tempChart.data.datasets[0].data = recentHistory.map(d => d.temperature);
    tempChart.update();

    // Update Inference state
    const stateEl = document.getElementById('live-state');
    
    if (inference) {
        stateEl.textContent = inference.state;
        if (inference.source && inference.source !== "None" && inference.source !== "Hardware Data") {
            stateEl.textContent += ` (${inference.source})`;
        }

        if (inference && inference.state && inference.state.includes("Detected")) {
            stateEl.className = 'state-event';
            heroTrend.textContent =  
                inference.source && inference.source.includes("Temp")
                    ? 'Temp Spike'
                    : 'Rising Rapidly';
            heroTrend.className = 'trend text-red';
        } else {
            stateEl.className = 'state-stable';
            heroTrend.textContent = latest.edaAvailable ? 'Stable' : 'Temp Monitor';
            heroTrend.className = 'trend neutral';
        }
    }

    // Update rolling baseline
    const edaValues = recentHistory.filter(d => d.edaAvailable).map(d => d.conductance);
    if (edaValues.length > 0) {
        baseline = edaValues.reduce((acc, v) => acc + v, 0) / edaValues.length;
        document.getElementById('live-baseline').textContent = `${baseline.toFixed(2)} µS`;
    } else {
        document.getElementById('live-baseline').textContent = 'N/A';
    }

    if (lastSync > 0) {
        document.getElementById('nav-last-sync').textContent = new Date(lastSync * 1000).toLocaleTimeString();
    } else {
        document.getElementById('nav-last-sync').textContent = 'Never';
    }
    const insightList = document.getElementById("insight-list");

    if (insightList && insights) {
        insightList.innerHTML = insights.map(text => `
            <li>
                <i class="fa-solid fa-circle-info text-blue"></i>
                <p>${text}</p>
            </li>
        `).join("");
    }
}

// Calibration Form Handling
document.getElementById('calibration-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    const offset = parseFloat(document.getElementById('cal-offset').value);
    const multiplier = parseFloat(document.getElementById('cal-multiplier').value);
    const statusEl = document.getElementById('cal-status');
    
    try {
        const response = await fetch('/api/calibrate', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ offset, multiplier })
        });
        if (response.ok) {
            statusEl.textContent = 'Calibration saved successfully!';
            statusEl.style.color = '#48bb78';
            setTimeout(() => statusEl.textContent = '', 3000);
        }
    } catch (error) {
        statusEl.textContent = 'Error saving calibration.';
        statusEl.style.color = '#f56565';
    }
});

// Fetch initially
fetchData();
// Poll every 2 seconds
setInterval(fetchData, 2000);
document.addEventListener("DOMContentLoaded", () => {
    const items = document.querySelectorAll('.sidebar-item');

    items.forEach(item => {
        item.addEventListener('click', () => {
            console.log("Clicked:", item.innerText); // DEBUG

            // remove active from all
            items.forEach(i => i.classList.remove('active'));

            // activate clicked
            item.classList.add('active');
        });
    });
});
document.addEventListener("DOMContentLoaded", () => {
    const navLinks = document.querySelectorAll("aside.sidebar nav a");

    navLinks.forEach(link => {
        link.addEventListener("click", (event) => {
            event.preventDefault();

            console.log("Clicked:", link.innerText.trim());

            navLinks.forEach(item => item.classList.remove("active"));
            link.classList.add("active");
        });
    });
});
document.addEventListener("DOMContentLoaded", () => {
    const navLinks = document.querySelectorAll("aside.sidebar nav a");
    const main = document.querySelector("main.main-content");

    const pages = {
        "Overview": main.innerHTML,

        "Events": `
            <header>
                <h1>Events</h1>
                <p class="subtitle">Detected hot flash events and symptom history.</p>
            </header>
            <section class="card full-width">
                <h2>Recent Events</h2>
                <p>No confirmed hot flash events yet.</p>
            </section>
        `,

        "Night Tracking": `
            <header>
                <h1>Night Tracking</h1>
                <p class="subtitle">Overnight monitoring.</p>
            </header>
            <section class="card full-width">
                <h2>Night Summary</h2>
                <p>No overnight session recorded yet.</p>
            </section>
        `,

        "Clinical Report": `
            <header>
                <h1>Clinical Report</h1>
                <p class="subtitle">Exportable summary.</p>
            </header>
            <section class="card full-width">
                <h2>Report Summary</h2>
                <p>Clinical report generation will appear here.</p>
            </section>
        `
    };

    navLinks.forEach(link => {
        link.addEventListener("click", (event) => {
            event.preventDefault();

            const pageName = link.innerText.trim();

            navLinks.forEach(item => item.classList.remove("active"));
            link.classList.add("active");

            if (pages[pageName]) {
                main.innerHTML = pages[pageName];
            }

            console.log("Switched to:", pageName);
        });
    });
});
