/**
 * molt - Copyright (C) 2012 Olivier Brunel
 *
 * variables.h
 * Copyright (C) 2012 Olivier Brunel <i.am.jack.mail@gmail.com>
 * 
 * This file is part of molt.
 *
 * molt is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * molt is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * molt. If not, see http://www.gnu.org/licenses/
 */

#ifndef VARIABLES_H
#define	VARIABLES_H

#ifdef	__cplusplus
extern "C" {
#endif

gchar *
var_get_value_nb (const gchar *file, GPtrArray *params, GError **error);


#ifdef	__cplusplus
}
#endif

#endif	/* VARIABLES_H */

