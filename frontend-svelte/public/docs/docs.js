const toast = document.querySelector('.toast');
let toastTimer = null;

async function copyText(value) {
  if (!value) return;
  try {
    await navigator.clipboard.writeText(value);
  } catch {
    const area = document.createElement('textarea');
    area.value = value;
    area.setAttribute('readonly', '');
    area.style.position = 'fixed';
    area.style.opacity = '0';
    document.body.appendChild(area);
    area.select();
    document.execCommand('copy');
    area.remove();
  }

  if (!toast) return;
  if (toastTimer) clearTimeout(toastTimer);
  toast.classList.add('visible');
  toastTimer = setTimeout(() => toast.classList.remove('visible'), 1600);
}

for (const button of document.querySelectorAll('[data-copy], [data-copy-source]')) {
  button.addEventListener('click', () => {
    const directValue = button.getAttribute('data-copy');
    const sourceId = button.getAttribute('data-copy-source');
    const sourceValue = sourceId
      ? document.getElementById(sourceId)?.textContent?.trim()
      : null;
    copyText(directValue || sourceValue || '');
  });
}

const tabButtons = Array.from(document.querySelectorAll('[data-tab]'));
const tabPanels = Array.from(document.querySelectorAll('[data-panel]'));
for (const button of tabButtons) {
  button.addEventListener('click', () => {
    const selected = button.getAttribute('data-tab');
    for (const candidate of tabButtons) {
      candidate.setAttribute(
        'aria-selected',
        String(candidate.getAttribute('data-tab') === selected)
      );
    }
    for (const panel of tabPanels) {
      panel.hidden = panel.getAttribute('data-panel') !== selected;
    }
  });
}
