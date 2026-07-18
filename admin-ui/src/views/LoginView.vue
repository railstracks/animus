<template>
  <div class="login-view d-flex align-center justify-center" style="min-height: 100vh; background: #1a1a2e;">
    <v-card class="pa-8" max-width="440" width="100%" elevation="12" rounded="lg">
      <div class="text-center mb-6">
        <h1 class="text-h4 font-weight-bold mb-2">Animus</h1>
        <p class="text-body-2 text-medium-emphasis">Sign in to continue</p>
      </div>

      <!-- Tabs for method selection -->
      <v-tabs v-model="tab" grow density="comfortable" class="mb-4" color="primary">
        <v-tab value="credentials">Credentials</v-tab>
        <v-tab value="token">Auth Token</v-tab>
      </v-tabs>

      <v-window v-model="tab">
        <!-- Credentials login -->
        <v-window-item value="credentials">
          <v-text-field
            v-model="credUsername"
            label="Username"
            density="comfortable"
            class="mb-3"
            autofocus
            @keyup.enter="$refs.passwordField.focus()"
          />
          <v-text-field
            ref="passwordField"
            v-model="credPassword"
            label="Password"
            type="password"
            density="comfortable"
            class="mb-4"
            @keyup.enter="doLogin"
          />
          <v-btn color="primary" block size="large" @click="doLogin" :loading="loading">
            Sign In
          </v-btn>
        </v-window-item>

        <!-- Static token -->
        <v-window-item value="token">
          <v-text-field
            v-model="tokenInput"
            label="Auth Token"
            hint="Set via ANIMUS_AUTH_TOKEN environment variable"
            type="password"
            density="comfortable"
            class="mb-4"
            autofocus
            @keyup.enter="doToken"
          />
          <v-btn color="primary" block size="large" @click="doToken" :loading="loading">
            Connect
          </v-btn>
        </v-window-item>
      </v-window>

      <v-alert v-if="error" type="error" variant="tonal" class="mt-4" density="comfortable">
        {{ error }}
      </v-alert>

      <!-- Setup prompt: token works but no users exist -->
      <v-alert
        v-if="setupAvailable"
        type="info" variant="tonal" class="mt-4" density="comfortable"
        closable
      >
        No user accounts exist yet.
        <a href="#" @click.prevent="showSetup = true">Create an admin account</a>
        for future sign-ins.
      </v-alert>

      <!-- Setup dialog -->
      <v-dialog v-model="showSetup" max-width="440">
        <v-card>
          <v-card-title>Create Admin Account</v-card-title>
          <v-card-text>
            <v-text-field v-model="setupUsername" label="Username" density="comfortable" class="mb-3" />
            <v-text-field v-model="setupPassword" label="Password" type="password" density="comfortable" class="mb-3" />
            <v-text-field v-model="setupConfirm" label="Confirm Password" type="password" density="comfortable" />
          </v-card-text>
          <v-card-actions>
            <v-spacer />
            <v-btn text @click="showSetup = false">Cancel</v-btn>
            <v-btn color="primary" @click="doSetup" :loading="loading">Create</v-btn>
          </v-card-actions>
        </v-card>
      </v-dialog>
    </v-card>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { useRouter } from 'vue-router';
import { useAuthStore } from '../stores/auth';

const router = useRouter();
const auth = useAuthStore();

const tab = ref('credentials');
const credUsername = ref('');
const credPassword = ref('');
const tokenInput = ref('');
const loading = ref(false);
const error = ref('');
const setupAvailable = ref(false);
const showSetup = ref(false);
const setupUsername = ref('');
const setupPassword = ref('');
const setupConfirm = ref('');

onMounted(async () => {
  await auth.checkStatus();

  // Already authenticated? Go to app.
  if (auth.isAuthenticated.value) {
    const me = await auth.fetchMe();
    if (me) {
      router.push('/');
      return;
    }
  }

  // No auth required? Go to app.
  if (!auth.authRequired.value) {
    router.push('/');
    return;
  }

  // Default to token tab if no users exist
  if (!auth.hasUsers.value) {
    tab.value = 'token';
  }
});

async function doLogin() {
  error.value = '';
  loading.value = true;
  const result = await auth.login(credUsername.value, credPassword.value);
  loading.value = false;
  if (result.ok) {
    router.push('/');
  } else {
    error.value = result.error || 'Login failed';
  }
}

async function doToken() {
  error.value = '';
  loading.value = true;

  // Verify the token works
  const resp = await fetch('/api/v1/auth/me', {
    headers: { Authorization: `Bearer ${tokenInput.value}` },
  });

  if (resp.ok) {
    auth.setToken(tokenInput.value);
    // Check if setup is available (no users)
    await auth.checkStatus();
    if (!auth.hasUsers.value) {
      setupAvailable.value = true;
    }
    loading.value = false;
    // Proceed to app
    router.push('/');
  } else {
    loading.value = false;
    error.value = 'Invalid token';
  }
}

async function doSetup() {
  error.value = '';
  if (setupPassword.value !== setupConfirm.value) {
    error.value = 'Passwords do not match';
    return;
  }
  if (setupPassword.value.length < 6) {
    error.value = 'Password must be at least 6 characters';
    return;
  }
  loading.value = true;
  const result = await auth.setup(setupUsername.value, setupPassword.value);
  loading.value = false;
  if (result.ok) {
    showSetup.value = false;
    setupAvailable.value = false;
    // Now logged in with user account
    router.push('/');
  } else {
    error.value = result.error || 'Setup failed';
  }
}
</script>
