<template>
  <div class="pa-4">
    <h1 class="text-h5 mb-4">User Management</h1>

    <v-card elevation="2" rounded="lg">
      <v-data-table
        :headers="headers"
        :items="users"
        :loading="loading"
        density="comfortable"
      >
        <template v-slot:top>
          <v-toolbar flat>
            <v-toolbar-title class="text-subtitle-1">Users</v-toolbar-title>
            <v-spacer />
            <v-btn color="primary" prepend-icon="mdi-plus" @click="openCreate">
              New User
            </v-btn>
          </v-toolbar>
        </template>

        <template v-slot:item.created_at="{ value }">
          {{ formatDate(value) }}
        </template>

        <template v-slot:item.actions="{ item }">
          <v-btn icon="mdi-pencil" variant="text" size="small" @click="openEdit(item)" />
          <v-btn icon="mdi-logout" variant="text" size="small" @click="revokeSessions(item)" title="Revoke sessions" />
          <v-btn icon="mdi-delete" variant="text" size="small" color="error" @click="confirmDelete(item)" />
        </template>
      </v-data-table>
    </v-card>

    <!-- Create/Edit Dialog -->
    <v-dialog v-model="dialog" max-width="480">
      <v-card>
        <v-card-title>{{ editing ? 'Edit User' : 'Create User' }}</v-card-title>
        <v-card-text>
          <v-text-field v-model="dialogForm.username" label="Username" density="comfortable" :disabled="editing" class="mb-3" />
          <v-text-field
            v-model="dialogForm.password"
            :label="editing ? 'New Password (leave blank to keep)' : 'Password'"
            type="password"
            density="comfortable"
            class="mb-3"
          />
          <v-select
            v-model="dialogForm.role"
            :items="['admin', 'viewer']"
            label="Role"
            density="comfortable"
          />
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn text @click="dialog = false">Cancel</v-btn>
          <v-btn color="primary" @click="saveUser" :loading="saving">{{ editing ? 'Save' : 'Create' }}</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- Delete Confirmation -->
    <v-dialog v-model="deleteDialog" max-width="400">
      <v-card>
        <v-card-title>Delete User</v-card-title>
        <v-card-text>
          Are you sure you want to delete "{{ deleteTarget?.username }}"? This will revoke all their sessions.
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn text @click="deleteDialog = false">Cancel</v-btn>
          <v-btn color="error" @click="doDelete">Delete</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <v-snackbar v-model="snackbar" :color="snackbarColor" :timeout="3000">
      {{ snackbarText }}
    </v-snackbar>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { useAuthStore } from '../stores/auth';

const auth = useAuthStore();
const token = auth.token;

const users = ref<any[]>([]);
const loading = ref(false);
const dialog = ref(false);
const editing = ref(false);
const editingId = ref('');
const saving = ref(false);
const deleteDialog = ref(false);
const deleteTarget = ref<any>(null);
const snackbar = ref(false);
const snackbarText = ref('');
const snackbarColor = ref('success');

const dialogForm = ref({
  username: '',
  password: '',
  role: 'admin',
});

const headers = [
  { title: 'Username', key: 'username' },
  { title: 'Role', key: 'role' },
  { title: 'Created', key: 'created_at' },
  { title: 'Actions', key: 'actions', sortable: false },
];

async function loadUsers() {
  loading.value = true;
  try {
    const resp = await fetch('/api/v1/users', {
      headers: { Authorization: `Bearer ${token}` },
    });
    users.value = await resp.json();
  } finally {
    loading.value = false;
  }
}

function openCreate() {
  editing.value = false;
  dialogForm.value = { username: '', password: '', role: 'admin' };
  dialog.value = true;
}

function openEdit(user: any) {
  editing.value = true;
  editingId.value = user.id;
  dialogForm.value = { username: user.username, password: '', role: user.role };
  dialog.value = true;
}

async function saveUser() {
  saving.value = true;
  try {
    if (editing.value) {
      const body: any = { role: dialogForm.value.role };
      if (dialogForm.value.password) body.password = dialogForm.value.password;
      const resp = await fetch(`/api/v1/users/${editingId.value}`, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${token}` },
        body: JSON.stringify(body),
      });
      const data = await resp.json();
      if (!resp.ok) throw new Error(data.error || 'Failed to update user');
      showSnackbar('User updated');
    } else {
      const resp = await fetch('/api/v1/users', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${token}` },
        body: JSON.stringify(dialogForm.value),
      });
      const data = await resp.json();
      if (!resp.ok) throw new Error(data.error || 'Failed to create user');
      showSnackbar('User created');
    }
    dialog.value = false;
    await loadUsers();
  } catch (e: any) {
    showSnackbar(e.message, 'error');
  } finally {
    saving.value = false;
  }
}

function confirmDelete(user: any) {
  deleteTarget.value = user;
  deleteDialog.value = true;
}

async function doDelete() {
  if (!deleteTarget.value) return;
  try {
    const resp = await fetch(`/api/v1/users/${deleteTarget.value.id}`, {
      method: 'DELETE',
      headers: { Authorization: `Bearer ${token}` },
    });
    if (!resp.ok) throw new Error('Failed to delete user');
    showSnackbar('User deleted');
    deleteDialog.value = false;
    await loadUsers();
  } catch (e: any) {
    showSnackbar(e.message, 'error');
  }
}

async function revokeSessions(user: any) {
  try {
    const resp = await fetch(`/api/v1/users/${user.id}/sessions`, {
      method: 'DELETE',
      headers: { Authorization: `Bearer ${token}` },
    });
    if (!resp.ok) throw new Error('Failed to revoke sessions');
    showSnackbar(`Sessions revoked for ${user.username}`);
  } catch (e: any) {
    showSnackbar(e.message, 'error');
  }
}

function showSnackbar(text: string, color: string = 'success') {
  snackbarText.value = text;
  snackbarColor.value = color;
  snackbar.value = true;
}

function formatDate(ts: number): string {
  if (!ts) return '—';
  return new Date(ts).toLocaleDateString();
}

onMounted(loadUsers);
</script>
