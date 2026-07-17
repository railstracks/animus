<template>
  <div class="login-view d-flex align-center justify-center" style="min-height: 100vh; background: #1a1a2e;">
    <v-card class="pa-8" max-width="420" width="100%" elevation="12" rounded="lg">
      <div class="text-center mb-6">
        <h1 class="text-h4 font-weight-bold mb-2">Animus</h1>
        <p class="text-body-2 text-medium-emphasis">{{ subtitle }}</p>
      </div>

      <!-- Step 1: Static token entry (for initial setup) -->
      <template v-if="mode === 'setup-token'">
        <v-text-field
          v-model="staticToken"
          label="Static Auth Token"
          hint="Set via ANIMUS_AUTH_TOKEN environment variable"
          type="password"
          density="comfortable"
          class="mb-4"
          @keyup.enter="proceedWithToken"
        />
        <v-btn color="primary" block size="large" @click="proceedWithToken" :loading="loading">
          Continue
        </v-btn>
      </template>

      <!-- Step 2: Create admin account -->
      <template v-else-if="mode === 'setup-account'">
        <v-alert type="info" variant="tonal" class="mb-4" density="comfortable">
          No user accounts exist yet. Create an admin account to get started.
        </v-alert>
        <v-text-field v-model="formUsername" label="Username" density="comfortable" class="mb-3" @keyup.enter="doSetup" />
        <v-text-field v-model="formPassword" label="Password" type="password" density="comfortable" class="mb-3" @keyup.enter="doSetup" />
        <v-text-field v-model="formPasswordConfirm" label="Confirm Password" type="password" density="comfortable" class="mb-4" @keyup.enter="doSetup" />
        <v-btn color="primary" block size="large" @click="doSetup" :loading="loading">
          Create Admin Account
        </v-btn>
      </template>

      <!-- Login form -->
      <template v-else>
        <v-text-field v-model="formUsername" label="Username" density="comfortable" class="mb-3" @keyup.enter="doLogin" />
        <v-text-field v-model="formPassword" label="Password" type="password" density="comfortable" class="mb-4" @keyup.enter="doLogin" />
        <v-btn color="primary" block size="large" @click="doLogin" :loading="loading">
          Sign In
        </v-btn>
      </template>

      <v-alert v-if="error" type="error" variant="tonal" class="mt-4" density="comfortable">
        {{ error }}
      </v-alert>
    </v-card>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue';
import { useRouter } from 'vue-router';
import { useAuthStore } from '@/stores/auth';

const router = useRouter();
const auth = useAuthStore();

const staticToken = ref('');
const formUsername = ref('');
const formPassword = ref('');
const formPasswordConfirm = ref('');
const loading = ref(false);
const error = ref('');

const mode = ref<'login' | 'setup-token' | 'setup-account'>('login');

const subtitle = computed(() => {
  if (mode.value === 'setup-token') return 'Enter your auth token to begin setup';
  if (mode.value === 'setup-account') return 'Create the first admin account';
  return 'Sign in to your account';
});

onMounted(async () => {
  await auth.checkStatus();

  if (!auth.authRequired) {
    // No auth needed — go straight to app
    router.push('/');
    return;
  }

  if (auth.hasUsers) {
    mode.value = 'login';
  } else if (auth.hasStaticToken) {
    // Has static token but no users — setup flow
    mode.value = 'setup-token';
  } else {
    // Auth required but no token and no users — misconfigured
    error.value = 'Authentication is required but no auth token or users are configured. Set ANIMUS_AUTH_TOKEN environment variable.';
  }

  // Try existing token
  if (auth.isAuthenticated) {
    const me = await auth.fetchMe();
    if (me) {
      router.push('/');
      return;
    }
  }
});

async function proceedWithToken() {
  error.value = '';
  loading.value = true;
  // Verify token works
  const resp = await fetch('/api/v1/auth/me', {
    headers: { Authorization: `Bearer ${staticToken.value}` },
  });
  loading.value = false;
  if (resp.ok) {
    auth.setToken(staticToken.value);
    mode.value = 'setup-account';
  } else {
    error.value = 'Invalid static token';
  }
}

async function doSetup() {
  error.value = '';
  if (formPassword.value !== formPasswordConfirm.value) {
    error.value = 'Passwords do not match';
    return;
  }
  if (formPassword.value.length < 6) {
    error.value = 'Password must be at least 6 characters';
    return;
  }
  loading.value = true;
  const result = await auth.setup(formUsername.value, formPassword.value);
  loading.value = false;
  if (result.ok) {
    router.push('/');
  } else {
    error.value = result.error || 'Setup failed';
  }
}

async function doLogin() {
  error.value = '';
  loading.value = true;
  const result = await auth.login(formUsername.value, formPassword.value);
  loading.value = false;
  if (result.ok) {
    router.push('/');
  } else {
    error.value = result.error || 'Login failed';
  }
}
</script>
