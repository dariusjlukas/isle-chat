import { useState, useEffect } from 'react';
import { Button, Input, Alert, Divider } from '@heroui/react';
import { useChatStore } from '../../stores/chatStore';
import { useSettingsNav } from '../common/settingsNavContext';
import * as api from '../../services/api';

export function PasswordSettings() {
  const updateUser = useChatStore((s) => s.updateUser);
  const user = useChatStore((s) => s.user);
  const navigateTo = useSettingsNav();
  const [hasPassword, setHasPassword] = useState<boolean | null>(null);
  const [passwordEnabled, setPasswordEnabled] = useState(false);
  const [mfaRequired, setMfaRequired] = useState(false);
  const [passwordPolicy, setPasswordPolicy] =
    useState<api.PasswordPolicy | null>(null);
  const [loading, setLoading] = useState(true);

  // Form state
  const [currentPassword, setCurrentPassword] = useState('');
  const [newPassword, setNewPassword] = useState('');
  const [confirmPassword, setConfirmPassword] = useState('');
  const [saving, setSaving] = useState(false);
  const [removing, setRemoving] = useState(false);
  const [error, setError] = useState('');
  const [success, setSuccess] = useState('');

  useEffect(() => {
    Promise.all([api.getPublicConfig(), api.getMe()])
      .then(([config, me]) => {
        setPasswordEnabled(config.auth_methods.includes('password'));
        setMfaRequired(config.mfa_required_password ?? false);
        if (config.password_policy) setPasswordPolicy(config.password_policy);
        setHasPassword(me.has_password ?? false);
      })
      .finally(() => setLoading(false));
  }, []);

  const handleSetPassword = async () => {
    setError('');
    setSuccess('');
    if (newPassword !== confirmPassword) {
      setError('Passwords do not match');
      return;
    }
    setSaving(true);
    try {
      await api.setPassword(newPassword);
      setHasPassword(true);
      updateUser({ has_password: true });
      setNewPassword('');
      setConfirmPassword('');
      setSuccess('Password created successfully');
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed to set password');
    } finally {
      setSaving(false);
    }
  };

  const handleChangePassword = async () => {
    setError('');
    setSuccess('');
    if (newPassword !== confirmPassword) {
      setError('Passwords do not match');
      return;
    }
    setSaving(true);
    try {
      await api.changePassword({
        current_password: currentPassword,
        new_password: newPassword,
      });
      setCurrentPassword('');
      setNewPassword('');
      setConfirmPassword('');
      setSuccess('Password changed successfully');
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed to change password');
    } finally {
      setSaving(false);
    }
  };

  const handleRemovePassword = async () => {
    if (
      !confirm(
        'Are you sure you want to remove your password? You will no longer be able to log in with a password.',
      )
    )
      return;
    setError('');
    setSuccess('');
    setRemoving(true);
    try {
      await api.deletePassword();
      setHasPassword(false);
      updateUser({ has_password: false });
      setCurrentPassword('');
      setNewPassword('');
      setConfirmPassword('');
      setSuccess('Password removed successfully');
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed to remove password');
    } finally {
      setRemoving(false);
    }
  };

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    if (hasPassword) {
      handleChangePassword();
    } else {
      handleSetPassword();
    }
  };

  if (loading) {
    return <div className='text-default-500 text-sm'>Loading...</div>;
  }

  if (!passwordEnabled) {
    return (
      <div className='text-default-500 text-sm'>
        Password authentication is not enabled on this server.
      </div>
    );
  }

  const needsMfaSetup = mfaRequired && !user?.has_totp && !hasPassword;

  return (
    <div className='space-y-4'>
      {needsMfaSetup && (
        <Alert color='warning' variant='flat'>
          <p className='text-sm'>
            This server requires two-factor authentication for password login.
            Please set up TOTP before adding a password.
          </p>
          <Button
            size='sm'
            color='warning'
            variant='solid'
            className='mt-2'
            onPress={() => navigateTo('two-factor')}
          >
            Set Up Two-Factor Auth
          </Button>
        </Alert>
      )}

      <p className='text-sm text-default-500'>
        {hasPassword
          ? 'Change your password below.'
          : "You don't have a password set. Create one to enable password-based login."}
      </p>

      {error && (
        <Alert color='danger' variant='flat'>
          {error}
        </Alert>
      )}
      {success && (
        <Alert color='success' variant='flat'>
          {success}
        </Alert>
      )}

      <form onSubmit={handleSubmit} className='space-y-4' autoComplete='off'>
        {hasPassword && (
          <Input
            label='Current Password'
            type='password'
            variant='bordered'
            value={currentPassword}
            onChange={(e) => setCurrentPassword(e.target.value)}
            autoComplete='current-password'
          />
        )}
        <Input
          label={hasPassword ? 'New Password' : 'Password'}
          type='password'
          variant='bordered'
          value={newPassword}
          onChange={(e) => setNewPassword(e.target.value)}
          autoComplete='new-password'
        />
        <Input
          label={hasPassword ? 'Confirm New Password' : 'Confirm Password'}
          type='password'
          variant='bordered'
          value={confirmPassword}
          onChange={(e) => setConfirmPassword(e.target.value)}
          autoComplete='new-password'
        />
        {passwordPolicy && (
          <p className='text-xs text-default-400'>
            {`Min ${passwordPolicy.min_length} chars`}
            {passwordPolicy.require_uppercase && ', uppercase'}
            {passwordPolicy.require_lowercase && ', lowercase'}
            {passwordPolicy.require_number && ', number'}
            {passwordPolicy.require_special && ', special character'}
          </p>
        )}
        <Button
          type='submit'
          color='primary'
          isLoading={saving}
          size='sm'
          isDisabled={needsMfaSetup}
        >
          {hasPassword ? 'Change Password' : 'Create Password'}
        </Button>
      </form>

      {hasPassword && (
        <>
          <Divider />
          <div>
            <p className='text-sm font-medium text-foreground mb-1'>
              Remove Password
            </p>
            <p className='text-xs text-default-400 mb-3'>
              Remove password-based login from your account. You must have
              another login method (passkey or browser key) configured.
            </p>
            <Button
              color='danger'
              variant='flat'
              size='sm'
              isLoading={removing}
              onPress={handleRemovePassword}
            >
              Remove Password
            </Button>
          </div>
        </>
      )}
    </div>
  );
}
