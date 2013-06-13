/* createrepo_c - Library of routines for manipulation with repodata
 * Copyright (C) 2013  Tomas Mlcoch
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#include <glib.h>
#include <assert.h>
#include "xml_file.h"
#include "error.h"
#include "xml_dump.h"
#include "compression_wrapper.h"
#include "xml_dump_internal.h"

#define XML_HEADER              "<?xml version=\""XML_DOC_VERSION \
                                "\" encoding=\""XML_ENCODING"\"?>\n"

#define XML_PRIMARY_HEADER      XML_HEADER"<metadata xmlns=\"" \
                                CR_XML_COMMON_NS"\" xmlns:rpm=\"" \
                                CR_XML_RPM_NS"\" packages=\"%d\">\n"
#define XML_FILELISTS_HEADER    XML_HEADER"<filelists xmlns=\"" \
                                CR_XML_FILELISTS_NS"\" packages=\"%d\">\n"
#define XML_OTHER_HEADER        XML_HEADER"<otherdata xmlns=\"" \
                                CR_XML_OTHER_NS"\" packages=\"%d\">\n"

#define XML_PRIMARY_FOOTER      "</metadata>"
#define XML_FILELISTS_FOOTER    "</filelists>"
#define XML_OTHER_FOOTER        "</otherdata>"

cr_XmlFile *
cr_xmlfile_sopen(const char *filename,
                 cr_XmlFileType type,
                 cr_CompressionType comtype,
                 cr_ContentStat *stat,
                 GError **err)
{
    cr_XmlFile *f;
    GError *tmp_err = NULL;

    assert(filename);
    assert(type < CR_XMLFILE_SENTINEL);
    assert(comtype < CR_CW_COMPRESSION_SENTINEL);
    assert(!err || *err == NULL);

    if (g_file_test(filename, G_FILE_TEST_EXISTS)) {
        g_set_error(err, CR_XML_FILE_ERROR, CRE_EXISTS,
                    "File already exists");
        return NULL;
    }

    CR_FILE *cr_f = cr_sopen(filename,
                             CR_CW_MODE_WRITE,
                             comtype,
                             stat,
                             &tmp_err);
    if (tmp_err) {
        g_propagate_prefixed_error(err, tmp_err, "Cannot open %s: ", filename);
        return NULL;
    }

    f = g_new0(cr_XmlFile, 1);
    f->f      = cr_f;
    f->type   = type;
    f->header = 0;
    f->footer = 0;
    f->pkgs   = 0;

    return f;
}

int
cr_xmlfile_set_num_of_pkgs(cr_XmlFile *f, long num, GError **err)
{
    assert(f);
    assert(f->header == 0);
    assert(num >= 0);
    assert(!err || *err == NULL);

    f->pkgs = num;
    return CRE_OK;
}

int
cr_xmlfile_write_xml_header(cr_XmlFile *f, GError **err)
{
    const char *xml_header;
    GError *tmp_err = NULL;

    assert(f);
    assert(!err || *err == NULL);
    assert(f->header == 0);

    switch (f->type) {
    case CR_XMLFILE_PRIMARY:
        xml_header = XML_PRIMARY_HEADER;
        break;
    case CR_XMLFILE_FILELISTS:
        xml_header = XML_FILELISTS_HEADER;
        break;
    case CR_XMLFILE_OTHER:
        xml_header = XML_OTHER_HEADER;
        break;
    default:
        assert(0);
    }

    if (cr_printf(&tmp_err, f->f, xml_header, f->pkgs) == CR_CW_ERR) {
        int code = tmp_err->code;
        g_propagate_prefixed_error(err, tmp_err, "Cannot write XML header: ");
        return code;
    }

    f->header = 1;

    return CRE_OK;
}

int
cr_xmlfile_write_xml_footer(cr_XmlFile *f, GError **err)
{
    const char *xml_footer;
    GError *tmp_err = NULL;

    assert(f);
    assert(!err || *err == NULL);
    assert(f->footer == 0);

    switch (f->type) {
    case CR_XMLFILE_PRIMARY:
        xml_footer = XML_PRIMARY_FOOTER;
        break;
    case CR_XMLFILE_FILELISTS:
        xml_footer = XML_FILELISTS_FOOTER;
        break;
    case CR_XMLFILE_OTHER:
        xml_footer = XML_OTHER_FOOTER;
        break;
    default:
        assert(0);
    }

    cr_puts(f->f, xml_footer, &tmp_err);
    if (tmp_err) {
        int code = tmp_err->code;
        g_propagate_prefixed_error(err, tmp_err, "Cannot write XML footer: ");
        return code;
    }

    f->footer = 1;

    return CRE_OK;
}

int
cr_xmlfile_add_pkg(cr_XmlFile *f, cr_Package *pkg, GError **err)
{
    char *xml;
    GError *tmp_err = NULL;

    assert(f);
    assert(pkg);
    assert(!err || *err == NULL);
    assert(f->footer == 0);

    switch (f->type) {
    case CR_XMLFILE_PRIMARY:
        xml = cr_xml_dump_primary(pkg, &tmp_err);
        break;
    case CR_XMLFILE_FILELISTS:
        xml = cr_xml_dump_filelists(pkg, &tmp_err);
        break;
    case CR_XMLFILE_OTHER:
        xml = cr_xml_dump_other(pkg, &tmp_err);
        break;
    default:
        assert(0);  // This shoud not happend
    }

    if (tmp_err) {
        int code = tmp_err->code;
        g_propagate_error(err, tmp_err);
        return code;
    }

    if (xml) {
        cr_xmlfile_add_chunk(f, xml, &tmp_err);
        g_free(xml);

        if (tmp_err) {
            int code = tmp_err->code;
            g_propagate_error(err, tmp_err);
            return code;
        }
    }

    return CRE_OK;
}

int
cr_xmlfile_add_chunk(cr_XmlFile *f, const char* chunk, GError **err)
{
    GError *tmp_err = NULL;

    assert(f);
    assert(!err || *err == NULL);
    assert(f->footer == 0);

    if (!chunk)
        return CRE_OK;

    if (f->header == 0) {
        cr_xmlfile_write_xml_header(f, &tmp_err);
        if (tmp_err) {
            int code = tmp_err->code;
            g_propagate_error(err, tmp_err);
            return code;
        }
    }

    cr_puts(f->f, chunk, &tmp_err);
    if (tmp_err) {
        int code = tmp_err->code;
        g_propagate_prefixed_error(err, tmp_err, "Error while write: ");
        return code;
    }

    return CRE_OK;
}

int
cr_xmlfile_close(cr_XmlFile *f, GError **err)
{
    GError *tmp_err = NULL;

    assert(!err || *err == NULL);

    if (!f)
        return CRE_OK;

    if (f->header == 0) {
        cr_xmlfile_write_xml_header(f, &tmp_err);
        if (tmp_err) {
            int code = tmp_err->code;
            g_propagate_error(err, tmp_err);
            return code;
        }
    }

    if (f->footer == 0) {
        cr_xmlfile_write_xml_footer(f, &tmp_err);
        if (tmp_err) {
            int code = tmp_err->code;
            g_propagate_error(err, tmp_err);
            return code;
        }
    }

    cr_close(f->f, &tmp_err);
    if (tmp_err) {
        int code = tmp_err->code;
        g_propagate_prefixed_error(err, tmp_err,
                "Error while closing a file: ");
        return code;
    }

    g_free(f);

    return CRE_OK;
}
