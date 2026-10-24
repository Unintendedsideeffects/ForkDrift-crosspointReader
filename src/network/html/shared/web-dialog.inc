<style>
.web-dialog {
  width: min(420px, calc(100vw - 32px));
  max-width: none;
  padding: 24px;
  border: 2px solid var(--black);
  background: var(--white);
  color: var(--black);
  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.2);
}
.web-dialog::backdrop {
  background: rgba(0, 0, 0, 0.6);
  backdrop-filter: blur(8px);
}
.web-dialog h2 {
  margin: 0 0 12px;
  color: var(--black);
  font-size: 1.2em;
  font-family: var(--font-display, Georgia, serif);
  font-weight: 700;
}
.web-dialog p {
  margin: 0;
  white-space: pre-wrap;
  overflow-wrap: anywhere;
  font-size: 0.95em;
  line-height: 1.4;
}
.web-dialog input[type="text"],
.web-dialog input[type="password"] {
  width: 100%;
  box-sizing: border-box;
  margin-top: 12px;
  padding: 8px;
  border: 1px solid var(--gray-7);
  background: var(--white);
  color: var(--black);
  font-family: inherit;
  font-size: 0.95em;
}
.web-dialog input[type="text"]:focus,
.web-dialog input[type="password"]:focus {
  outline: 2px solid var(--black);
}
.web-dialog-actions {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
  margin-top: 20px;
}
.web-dialog-actions button {
  min-width: 72px;
  padding: 8px 14px;
  border: 2px solid var(--black);
  background: var(--black);
  color: var(--white);
  font-family: var(--font-mono, monospace);
  font-size: 0.85em;
  font-weight: 700;
  cursor: pointer;
}
.web-dialog-actions button:hover {
  background: var(--white);
  color: var(--black);
}
.web-dialog-actions #webDialogCancel {
  background: var(--white);
  color: var(--black);
  border: 2px solid var(--gray-7);
}
.web-dialog-actions #webDialogCancel:hover {
  border-color: var(--black);
  background: var(--gray-9);
}
@media (max-width: 600px) {
  .web-dialog {
    width: auto;
    margin-right: 1.5rem;
    margin-left: 1.5rem;
  }
  .web-dialog-actions {
    flex-direction: column-reverse;
  }
}
</style>
<dialog id="webMessageDialog" class="web-dialog" aria-labelledby="webDialogTitle" aria-describedby="webDialogMessage">
  <form method="dialog">
    <h2 id="webDialogTitle"></h2>
    <p id="webDialogMessage"></p>
    <input type="text" id="webDialogInput" style="display:none;" />
    <div class="web-dialog-actions">
      <button id="webDialogCancel" value="cancel" type="button">Cancel</button>
      <button id="webDialogConfirm" value="confirm" type="submit">OK</button>
    </div>
  </form>
</dialog>
<script>
let webDialogQueue = Promise.resolve();

(function() {
  const dialog = document.getElementById('webMessageDialog');
  const cancelBtn = document.getElementById('webDialogCancel');
  if (dialog) {
    let clickTarget = null;
    const recordTarget = function(event) {
      clickTarget = event.target;
    };
    dialog.addEventListener('pointerdown', recordTarget);
    dialog.addEventListener('mousedown', recordTarget);
    // The dialog's own padding also reports target === dialog, so only a press and release
    // outside its box count as a backdrop click.
    const outsideBox = function(event) {
      const r = dialog.getBoundingClientRect();
      return event.clientX < r.left || event.clientX > r.right || event.clientY < r.top || event.clientY > r.bottom;
    };
    dialog.addEventListener('click', function(event) {
      const clickedBackdrop = clickTarget === dialog && event.target === dialog && outsideBox(event);
      clickTarget = null;
      if (clickedBackdrop) dialog.close('cancel');
    });
    dialog.addEventListener('close', function() {
      clickTarget = null;
    });
  }
  if (cancelBtn && dialog) {
    cancelBtn.addEventListener('click', function() {
      dialog.close('cancel');
    });
  }
})();

function showWebDialog(title, message, options) {
  const isConfirm = typeof options === 'object' ? !!options.confirm : !!options;
  const isPrompt = typeof options === 'object' ? !!options.prompt : false;
  const isPassword = typeof options === 'object' ? !!options.password : false;
  const defaultVal = typeof options === 'object' && options.defaultValue ? options.defaultValue : '';

  const result = webDialogQueue.then(() => new Promise(resolve => {
    const dialog = document.getElementById('webMessageDialog');
    const titleEl = document.getElementById('webDialogTitle');
    const msgEl = document.getElementById('webDialogMessage');
    const inputEl = document.getElementById('webDialogInput');
    const cancelBtn = document.getElementById('webDialogCancel');
    const confirmBtn = document.getElementById('webDialogConfirm');

    if (!dialog) {
      resolve(false);
      return;
    }

    if (titleEl) titleEl.textContent = title;
    if (msgEl) msgEl.textContent = String(message || '');
    if (cancelBtn) cancelBtn.hidden = !isConfirm && !isPrompt;
    if (confirmBtn) confirmBtn.textContent = isConfirm ? 'Confirm' : 'OK';
    if (inputEl) {
      inputEl.style.display = isPrompt ? 'block' : 'none';
      inputEl.type = isPrompt && isPassword ? 'password' : 'text';
      inputEl.autocomplete = isPrompt && isPassword ? 'current-password' : 'off';
      inputEl.value = defaultVal;
    }
    dialog.returnValue = '';
    dialog.addEventListener('close', () => {
      if (isPrompt) {
        resolve(dialog.returnValue === 'confirm' ? (inputEl ? inputEl.value : '') : null);
      } else {
        resolve(dialog.returnValue === 'confirm');
      }
    }, { once: true });
    dialog.showModal();
    if (isPrompt && inputEl) {
      inputEl.focus();
      inputEl.select();
    }
  }));
  webDialogQueue = result.then(() => undefined, () => undefined);
  return result;
}

function showWebAlert(message, title) {
  return showWebDialog(title || 'Notice', message, false).then(() => undefined);
}

function showWebConfirm(message, title) {
  return showWebDialog(title || 'Confirm action', message, true);
}

function showWebPrompt(message, defaultValue, options) {
  let defaultVal = defaultValue;
  let opts = options;
  if (typeof defaultValue === 'object' && defaultValue !== null && options === undefined) {
    opts = defaultValue;
    defaultVal = '';
  }
  opts = typeof opts === 'object' && opts !== null ? opts : {};
  const isPassword = opts.password === true;
  const title = (typeof options === 'string' ? options : opts.title) || 'Prompt';
  return showWebDialog(title, message, {
    prompt: true,
    defaultValue: defaultVal || '',
    password: isPassword
  });
}
</script>
