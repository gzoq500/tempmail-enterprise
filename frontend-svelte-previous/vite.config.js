import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';

// Fix Svelte 5 runtime: tree-shaking removes firstChild accessor init
const svelteRuntimeFix = {
  name: 'svelte-runtime-fix',
  enforce: 'post',
  renderChunk(code) {
    if (code.includes('first_child_getter') && !code.includes('first_child_getter =')) {
      code = code.replace(
        /var\s+first_child_getter\s*;/,
        'var first_child_getter = Object.getOwnPropertyDescriptor(Node.prototype, "firstChild").get;'
      );
    }
    if (code.includes('is_firefox') && /var\s+is_firefox\s*;/.test(code)) {
      code = code.replace(
        /var\s+is_firefox\s*;/,
        'var is_firefox = typeof InstallTrigger !== "undefined";'
      );
    }
    return code;
  }
};

export default defineConfig({
  plugins: [svelte(), svelteRuntimeFix],
  build: { minify: 'esbuild' }
});
