/*	$NetBSD: pam_ksu.c,v 1.9.36.2 2023/09/08 09:06:21 martin Exp $	*/

/*-
 * Copyright (c) 2002 Jacques A. Vidrine <nectar@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/cdefs.h>
#ifdef __FreeBSD__
__FBSDID("$FreeBSD: src/lib/libpam/modules/pam_ksu/pam_ksu.c,v 1.5 2004/02/10 10:13:21 des Exp $");
#else
__RCSID("$NetBSD: pam_ksu.c,v 1.9.36.2 2023/09/08 09:06:21 martin Exp $");
#endif

#include <sys/param.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <krb5/krb5.h>

#define PAM_SM_AUTH
#define PAM_SM_CRED
#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <security/pam_mod_misc.h>

static const char superuser[] = "root";

#define PASSWORD_PROMPT	"%s's password:"

static void	log_krb5(krb5_context, krb5_error_code, const char *, ...)
    __printflike(3, 4);
static krb5_error_code	get_su_principal(krb5_context, const char *,
    const char *, char **, krb5_principal *);
static int	auth_krb5(pam_handle_t *, krb5_context, const char *,
		    krb5_principal);

PAM_EXTERN int
pam_sm_authenticate(pam_handle_t *pamh, int flags __unused,
    int argc __unused, const char *argv[] __unused)
{
	krb5_boolean	 allow_homedir;
	krb5_context	 context;
	krb5_principal	 su_principal;
	const char	*user;
	const void	*ruser;
	char		*su_principal_name;
	krb5_error_code	 rv;
	int		 pamret;

	pamret = pam_get_user(pamh, &user, NULL);
	if (pamret != PAM_SUCCESS)
		return (pamret);
	PAM_LOG("Got user: %s", user);
	pamret = pam_get_item(pamh, PAM_RUSER, &ruser);
	if (pamret != PAM_SUCCESS)
		return (pamret);
	PAM_LOG("Got ruser: %s", (const char *)ruser);
	allow_homedir = krb5_set_home_dir_access(NULL, FALSE);
	rv = krb5_init_context(&context);
	if (rv != 0) {
		log_krb5(context, rv, "krb5_init_context failed");
		pamret = PAM_SERVICE_ERR;
		goto out;
	}
	rv = get_su_principal(context, user, ruser, &su_principal_name, &su_principal);
	if (rv != 0) {
		pamret = PAM_AUTH_ERR;
		goto out;
	}
	PAM_LOG("kuserok: %s -> %s", su_principal_name, user);
	(void)krb5_set_home_dir_access(NULL, TRUE); /* ~user/.k5login */
	rv = krb5_kuserok(context, su_principal, user);
	(void)krb5_set_home_dir_access(NULL, FALSE);
	pamret = rv ? auth_krb5(pamh, context, su_principal_name, su_principal) : PAM_AUTH_ERR;
	free(su_principal_name);
	krb5_free_principal(context, su_principal);
	krb5_free_context(context);
out:	(void)krb5_set_home_dir_access(NULL, allow_homedir);
	return (pamret);
}

PAM_EXTERN int
pam_sm_setcred(pam_handle_t *pamh __unused, int flags __unused,
    int ac __unused, const char *av[] __unused)
{

	return (PAM_SUCCESS);
}

/* Authenticate using Kerberos 5.
 *   pamh              -- The PAM handle.
 *   context           -- An initialized krb5_context.
 *   su_principal_name -- The target principal name, used only for password prompts.
 *              If NULL, the password prompts will not include a principal
 *              name.
 *   su_principal      -- The target krb5_principal.
 * Note that a valid keytab in the default location with a host entry
 * must be available, and that the PAM application must have sufficient
 * privileges to access it.
 * Returns PAM_SUCCESS if authentication was successful, or an appropriate
 * PAM error code if it was not.
 */
static int
auth_krb5(pam_handle_t *pamh, krb5_context context, const char *su_principal_name,
    krb5_principal su_principal)
{
	krb5_creds	 creds;
	krb5_get_init_creds_opt *gic_opt;
	krb5_verify_init_creds_opt vic_opt;
	const char	*pass;
	char		 prompt[80];
	krb5_error_code	 rv;
	int		 pamret;

	rv = krb5_get_init_creds_opt_alloc(context, &gic_opt);
	if (rv != 0) {
		log_krb5(context, rv, "krb5_get_init_creds_opt_alloc");
		return (PAM_SERVICE_ERR);
	}
	krb5_verify_init_creds_opt_init(&vic_opt);
	if (su_principal_name != NULL)
		(void)snprintf(prompt, sizeof(prompt), PASSWORD_PROMPT,
		    su_principal_name);
	else
		(void)snprintf(prompt, sizeof(prompt), "Password:");
	pass = NULL;
	pamret = pam_get_authtok(pamh, PAM_AUTHTOK, &pass, prompt);
	if (pamret != PAM_SUCCESS)
		return (pamret);
	rv = krb5_get_init_creds_password(context, &creds, su_principal,
	    pass, NULL, NULL, 0, NULL, gic_opt);
	if (rv != 0) {
		log_krb5(context, rv, "krb5_get_init_creds_password");
		return (PAM_AUTH_ERR);
	}
	krb5_verify_init_creds_opt_set_ap_req_nofail(&vic_opt, 1);
	rv = krb5_verify_init_creds(context, &creds, NULL, NULL, NULL,
	    &vic_opt);
	krb5_free_cred_contents(context, &creds);
	if (rv != 0) {
		log_krb5(context, rv, "krb5_verify_init_creds");
		return (PAM_AUTH_ERR);
	}
	return (PAM_SUCCESS);
}

static void
log_krb5(krb5_context ctx, krb5_error_code err, const char *fmt, ...)
{
	char b1[1024], b2[1024];
	const char *errtxt;
	va_list ap;
 
	va_start(ap, fmt);
	vsnprintf(b1, sizeof(b1), fmt, ap);
	va_end(ap);
	if (ctx)
		errtxt = krb5_get_error_message(ctx, err);
	else
		errtxt = NULL;
	if (errtxt != NULL) {
		snprintf(b2, sizeof(b2), "%s", errtxt);
		krb5_free_error_message(ctx, errtxt);
	} else {
		snprintf(b2, sizeof(b2), "unknown %d", (int)err);
	}
	PAM_LOG("%s (%s)", b1, b2);
}

/* Determine the target principal given the current user and the target user.
 *   context           -- An initialized krb5_context.
 *   target_user       -- The target username.
 *   current_user      -- The current username.
 *   su_principal_name -- (out) The target principal name.
 *   su_principal      -- (out) The target krb5_principal.
 * When the target user is `root', the target principal will be a `root
 * instance', e.g. `luser/root@REA.LM'.  Otherwise, the target principal
 * will simply be the current user's default principal name.  Note that
 * in any case, if KRB5CCNAME is set and a credentials cache exists, the
 * principal name found there will be the `starting point', rather than
 * the ruser parameter.
 *
 * Returns 0 for success, or a com_err error code on failure.
 */
static krb5_error_code
get_su_principal(krb5_context context, const char *target_user, const char *current_user,
    char **su_principal_name, krb5_principal *su_principal)
{
	krb5_principal	 default_principal;
	krb5_ccache	 ccache;
	char		*principal_name, *ccname, *p;
	krb5_error_code	 rv;
	uid_t		 euid, ruid;

	*su_principal = NULL;
	default_principal = NULL;
	/* Unless KRB5CCNAME was explicitly set, we won't really be able
	 * to look at the credentials cache since krb5_cc_default will
	 * look at getuid().
	 */
	ruid = getuid();
	euid = geteuid();
	rv = seteuid(ruid);
	if (rv != 0)
		return (errno);
	p = getenv("KRB5CCNAME");
	if (p != NULL)
		ccname = strdup(p);
	else
		(void)asprintf(&ccname, "%s%lu", KRB5_DEFAULT_CCROOT, (unsigned long)ruid);
	if (ccname == NULL)
		return (errno);
	rv = krb5_cc_resolve(context, ccname, &ccache);
	free(ccname);
	if (rv == 0) {
		rv = krb5_cc_get_principal(context, ccache, &default_principal);
		krb5_cc_close(context, ccache);
		if (rv != 0)
			default_principal = NULL; /* just to be safe */
	}
	rv = seteuid(euid);
	if (rv != 0)
		return (errno);
	if (default_principal == NULL) {
		rv = krb5_make_principal(context, &default_principal, NULL, current_user, NULL);
		if (rv != 0) {
			PAM_LOG("Could not determine default principal name.");
			return (rv);
		}
	}
	/* Now that we have some principal, if the target account is
	 * `root', then transform it into a `root' instance, e.g.
	 * `user@REA.LM' -> `user/root@REA.LM'.
	 */
	rv = krb5_unparse_name(context, default_principal, &principal_name);
	krb5_free_principal(context, default_principal);
	if (rv != 0) {
		log_krb5(context, rv, "krb5_unparse_name");
		return (rv);
	}
	PAM_LOG("Default principal name: %s", principal_name);
	if (strcmp(target_user, superuser) == 0) {
		p = strrchr(principal_name, '@');
		if (p == NULL) {
			PAM_LOG("malformed principal name `%s'", principal_name);
			free(principal_name);
			return (rv);
		}
		*p++ = '\0';
		*su_principal_name = NULL;
		(void)asprintf(su_principal_name, "%s/%s@%s", principal_name, superuser, p);
		free(principal_name);
	} else
		*su_principal_name = principal_name;

	if (*su_principal_name == NULL)
		return (errno);
	rv = krb5_parse_name(context, *su_principal_name, &default_principal);
	if (rv != 0) {
		log_krb5(context, rv, "krb5_parse_name `%s'",
		    *su_principal_name);
		return (rv);
	}
	PAM_LOG("Target principal name: %s", *su_principal_name);
	*su_principal = default_principal;
	return (0);
}

PAM_MODULE_ENTRY("pam_ksu");
