// Create WebSocket connection
const socket = new WebSocket('ws://weather1.local/ws');
const elements = {
    temperature: document.getElementById('temperature'),
    pressure: document.getElementById('pressure'),
    altitude: document.getElementById('altitude'),
    heading: document.getElementById('heading'),
    magnetic: document.getElementById('magnetic')
};

// Connection opened
socket.addEventListener('open', (event) => {
    sendToServer('data1');
    console.log('Connected to WebSocket server');
});

// Listen for messages from server
socket.addEventListener('message', (event) => {
    try {
        const data = JSON.parse(event.data);
        updateReadings(data);
    } catch (e) {
        console.error('Error parsing websocket data:', e);
    }
});

// Handle errors
socket.addEventListener('error', (event) => {
    console.error('WebSocket error:', event);
});

// Connection closed
socket.addEventListener('close', (event) => {
    console.log('Disconnected from WebSocket server');
    // Attempt to reconnect after 5 seconds
    setTimeout(() => {
        window.location.reload();
    }, 5000);
});

// Function to send data to server
function sendToServer(data) {
    if (socket.readyState === WebSocket.OPEN) {
        socket.send(data);
    } else {
        console.error('WebSocket is not open');
    }
}

function updateReadings(data) {
    if (data.temperature) {
        elements.temperature.textContent = 
            `Temperature: ${data.temperature.c.toFixed(2)} \u00B0C, ${data.temperature.f.toFixed(2)} \u00B0F`;
    }
    if (data.pressure) {
        elements.pressure.textContent = 
            `Pressure: ${data.pressure.pa.toLocaleString()} Pa, ${data.pressure.inhg.toFixed(2)} inHg`;
    }
    if (data.altitude) {
        elements.altitude.textContent = 
            `Altitude: ${data.altitude.m.toFixed(1)} m, ${data.altitude.ft.toFixed(0)} ft`;
    }
    if (data.heading) {
        elements.heading.textContent = 
            `Heading: ${Math.round(data.heading)} degrees`;
    }
    if (data.magnetic) {
        elements.magnetic.textContent = 
            `Magnetic field x,y,z: ${data.magnetic.x.toFixed(2)}, ${data.magnetic.y.toFixed(2)}, ${data.magnetic.z.toFixed(2)} mG`;
    }
}