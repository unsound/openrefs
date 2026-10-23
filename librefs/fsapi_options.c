/*-
 * fsapi.c - ReFS public file system API option parsing definitions.
 *
 * Copyright (c) 2026 Erik Larsson
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the source
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsapi.h"

sys_bool fsapi_options_parse_generic_unsigned_integer_value(
		const char *const name,
		const u64 max_value,
		const char *const value,
		const size_t value_length,
		u64 *const out_value,
		sys_bool *const out_value_valid)
{
	sys_bool res = SYS_FALSE;
	u8 base;
	size_t i;
	u64 parsed_value = 0;

	if(!value) {
		fsapi_options_log_missing_value(name,
			" (expects an unsigned integer value)");
		goto out;
	}

	if(*out_value_valid) {
		fsapi_options_log_duplicate_definition(name);
		goto out;
	}

	if(value_length > 2 && value[0] == '0' && value[1] == 'x') {
		base = 16;
		i = 2;
	}
	else if(value_length > 1 && value[0] == '0') {
		base = 8;
		i = 1;
	}
	else {
		base = 10;
		i = 0;
	}

	for(; i < value_length; ++i) {
		const char c = value[i];
		u8 digit;

		if(c >= '0' && c <= '9') {
			digit = c - '0';
		}
		else if(base > 10 && c >= 'A' && c <= 'F') {
			digit = 10 + (c - 'A');
		}
		else if(base > 10 && c >= 'a' && c <= 'f') {
			digit = 10 + (c - 'a');
		}
		else {
			fsapi_options_log_invalid_value(value_length, value,
				name,
				" (non-digits in integer value)");
			goto out;
		}

		parsed_value = parsed_value * ((u64) base) + ((u64) digit);
	}

	if(max_value && parsed_value > max_value) {
		fsapi_options_log_invalid_value(value_length, value, name,
			" (exceeds the maximum value of the option)");
		goto out;
	}

	res = SYS_TRUE;
	*out_value = parsed_value;
	*out_value_valid = SYS_TRUE;
out:
	return res;
}

int fsapi_options_parse_custom_mount_option(
		const char *const name,
		const size_t name_length,
		const char *const value,
		const size_t value_length,
		fsapi_options_specification *const spec,
		int (*const parse_unrecognized_option)(
			void *context,
			const char *name,
			size_t name_length,
			const char *value,
			size_t value_length),
		void *const parse_unrecognized_option_context,
		void **const out_custom_mount_options)
{
	int err = 0;
	void *custom_mount_options = NULL;
	sys_bool cleanup_custom_mount_options = SYS_FALSE;
	sys_bool special_mount_option_parsed = SYS_FALSE;
	size_t i = 0;
	fsapi_option_specification_entry *matching_entry = NULL;

	if(*out_custom_mount_options) {
		/* Allow appending to previously created custom mount
		 * options. */
		custom_mount_options = *out_custom_mount_options;
	}
	else {
		err = fsapi_options_init(
			/* void **out_custom_mount_options */
			&custom_mount_options);
		if(err) {
			goto out;
		}

		cleanup_custom_mount_options = SYS_TRUE;
	}

	err = fsapi_options_parse_special_mount_option(
		/* const char *name */
		name,
		/* size_t name_length */
		name_length,
		/* const char *const value */
		value,
		/* const size_t value_length */
		value_length,
		/* void *custom_mount_options */
		custom_mount_options,
		/* sys_bool *out_parsed */
		&special_mount_option_parsed);
	if(err) {
		goto out;
	}
	else if(special_mount_option_parsed) {
		*out_custom_mount_options = custom_mount_options;
		cleanup_custom_mount_options = SYS_FALSE;
		goto out;
	}

	/* Match one of the spec entries against the name. */
	for(i = 0; i < spec->entries_count; ++i) {
		const size_t entry_name_length =
			strlen(spec->entries[i].option_name);

		if(entry_name_length == name_length &&
			!memcmp(spec->entries[i].option_name, name,
			name_length))
		{
			matching_entry = &spec->entries[i];
			break;
		}
	}

	if(matching_entry && matching_entry->parse_value(
		/* void *custom_mount_options */
		custom_mount_options,
		/* const char *value */
		value,
		/* size_t value_length */
		value_length));
	else if(!matching_entry && parse_unrecognized_option) {
		/* If there is no matching entry and there is an unrecognized
		 * option handler, then call that handler for the option. */
		err = parse_unrecognized_option(
			/* void *context */
			parse_unrecognized_option_context,
			/* const char *name */
			name,
			/* size_t name_length */
			name_length,
			/* const char *value */
			value,
			/* size_t value_length */
			value_length);
		if(err) {
			goto out;
		}
	}
	else {
		/* If there's no matching entry, or the value didn't parse
		 * properly, then the option is unknown. We signal this by
		 * returning NULL in '*out_custom_mount_options' rather than
		 * returning an error as this is part of normal operation.
		 * The caller must take care to handle this condition and save a
		 * copy of any preexisting mount options pointer before calling
		 * this function. */

		if(!matching_entry) {
			sys_log_error("Unrecognized mount option "
				"\"%" PRIbs "\".",
				PRAbs(name_length, name));
		}

		/* If the mount option is recognized but doesn't have a
		 * valid value, then we expect the 'parse_value'
		 * callback to emit an error message with more specific
		 * details. */

		*out_custom_mount_options = NULL;
		goto out;
	}

	*out_custom_mount_options = custom_mount_options;
	cleanup_custom_mount_options = SYS_FALSE;
out:
	if(cleanup_custom_mount_options) {
		fsapi_options_deinit(
			/* void **custom_mount_optionsp */
			&custom_mount_options);
	}

	return err;
}

int fsapi_options_parse_custom_mount_options(
		const char *const options,
		const size_t options_length,
		const sys_bool comma_separated,
		fsapi_options_specification *const spec,
		int (*const parse_unrecognized_option)(
			void *context,
			const char *name,
			size_t name_length,
			const char *value,
			size_t value_length),
		void *const parse_unrecognized_option_context,
		void **const out_custom_mount_options)
{
	int err = 0;
	sys_bool cleanup_custom_mount_options = SYS_FALSE;
	void *custom_mount_options = NULL;
	size_t i = 0;
	size_t value_length = 0;
	char *value_alloc = NULL;

	if(*out_custom_mount_options) {
		/* Allow appending to previously created custom mount
		 * options. */
		custom_mount_options = *out_custom_mount_options;
	}
	else {
		err = fsapi_options_init(
			/* void **out_custom_mount_options */
			&custom_mount_options);
		if(err) {
			goto out;
		}

		cleanup_custom_mount_options = SYS_TRUE;
	}

	do {
		const char *const name = &options[i];
		size_t j;
		size_t name_length;
		const char *value = NULL;
		void *local_custom_mount_options = NULL;

		/* Parse the name part of this option. If options are comma
		 * separated we don't parse escape sequences in the name, as
		 * nobody in their right mind would introduce option names with
		 * commas in them. */
		for(j = i; j < options_length && options[j] != '=' &&
			(!comma_separated || options[j] != ','); ++j);

		name_length = j - i;
		if(!name_length) {
			/* Silently ignore empty options. Debatable, it could be
			 * considered an error. */
			break;
		}

		i = j;
		value_length = 0;

		if(i < options_length && options[i] == '=') {
			/* Parse the value part of this option. For the value
			 * part we parse comma escapes.  */

			value = &options[++i];

			if(comma_separated) {
				sys_bool escape = SYS_FALSE;
				sys_bool has_escapes = SYS_FALSE;
				size_t escaped_length = 0;

				/* Iterate over characters until we find the
				 * next comma, taking into account escape
				 * sequences. */

				for(j = i; j < options_length; ++j) {
					const char c = options[j];

					if(c == ',' && !escape) {
						break;
					}
					else if(escape) {
						has_escapes = SYS_TRUE;
						--escaped_length;
						/* Reset 'escape' state
						 * variable. */
						escape = SYS_FALSE;
					}
					else if(c == '\\') {
						escape = SYS_TRUE;
					}

					++escaped_length;
				}

				value_length = j - i;
				if(j < options_length && options[j] == ',') {
					++j;
				}
				i = j;

				if(has_escapes) {
					err = sys_malloc(escaped_length + 1,
						&value_alloc);
					if(err) {
						goto out;
					}

					escaped_length = 0;
					escape = SYS_FALSE;

					for(j = 0; j < value_length; ++j) {
						const char c = value[j];

						if(escape) {
							/* Backtrack one
							 * position and store
							 * the escaped character
							 * in place of the
							 * escape character. */
							--escaped_length;

							/* Reset 'escape' state
							 * variable. */
							escape = SYS_FALSE;
						}
						else if(c == '\\') {
							escape = SYS_TRUE;
						}

						value_alloc[escaped_length] = c;

						++escaped_length;
					}

					value_alloc[escaped_length] = '\0';

					value = value_alloc;
					value_length = escaped_length;
				}
			}
			else {
				i = value_length = options_length - i;
			}
		}
		else if(comma_separated && options[i] == ',') {
			++i;
		}

		local_custom_mount_options = custom_mount_options;

		err = fsapi_options_parse_custom_mount_option(
			/* const char *name */
			name,
			/* size_t name_length */
			name_length,
			/* const char *value */
			value,
			/* size_t value_length */
			value_length,
			/* fsapi_options_specification *spec */
			spec,
			/* int (*parse_unrecognized_option)(
			 *     void *context,
			 *     const char *name,
			 *     size_t name_length,
			 *     const char *value,
			 *     size_t value_length) */
			parse_unrecognized_option,
			/* void *parse_unrecognized_option_context */
			parse_unrecognized_option_context,
			/* void **out_custom_mount_options */
			&local_custom_mount_options);
		if(err || !local_custom_mount_options) {
			goto out;
		}

		custom_mount_options = local_custom_mount_options;

		if(value_alloc) {
			sys_free(value_length + 1, &value_alloc);
			value_length = 0;
		}
	} while(i < options_length);

	*out_custom_mount_options = custom_mount_options;
	cleanup_custom_mount_options = SYS_FALSE;
out:
	if(value_alloc) {
		sys_free(value_length + 1, &value_alloc);
	}

	if(cleanup_custom_mount_options) {
		fsapi_options_deinit(
			/* void **custom_mount_optionsp */
			&custom_mount_options);
	}

	return err;
}

int fsapi_options_release_custom_mount_options(
		void **const custom_mount_optionsp)
{
	fsapi_options_deinit(
		/* void **custom_mount_optionsp */
		custom_mount_optionsp);

	return 0;
}
