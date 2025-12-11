let isScanning = false;

function startHRPoll() {
    setInterval(() => {
        fetch('/api/hr')
            .then(r => r.json())
            .then(data => {
                const el = document.getElementById('heart-rate-value');
                if (el) el.innerText = data.hr > 0 ? data.hr : '--';

                const el2 = document.getElementById('hr-display');
                if (el2) el2.innerText = data.hr > 0 ? data.hr : '--';
            })
            .catch(e => console.error(e));
    }, 1000);
}

function startThemePoll() {
    // Poll theme every 2 seconds
    setInterval(() => {
        fetch('/api/theme')
            .then(r => r.json())
            .then(data => {
                if (data.theme && document.body) {
                    const currentClass = document.body.className;
                    const newClass = 'theme-' + data.theme;
                    if (currentClass !== newClass) {
                        document.body.className = newClass;
                    }
                }
            })
            .catch(e => { });
    }, 2000);
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
