/*
 * All Rights Reserved.
 *
 * ClearSilver Templating System
 *
 * This code is made available under the terms of the ClearSilver License.
 * http://www.clearsilver.net/license.hdf
 *
 */

#include <ctype.h>
#include <string.h>
#include "htmlparser/htmlparser.h"
#include "neo_err.h"
#include "neo_str.h"
#include "neo_auto.h"

/* This structure is used to map an HTTP content type to the htmlparser mode
 * that should be used for parsing it.
 */
static struct _neos_content_map
{
  char *content_type;  /* mime type of input */
  int parser_mode;  /* corresponding htmlparser mode */
} ContentTypeList[] = {
  {"none", -1},
  {"text/html", HTMLPARSER_MODE_HTML},
  {"text/xml", HTMLPARSER_MODE_HTML},
  {"text/plain", HTMLPARSER_MODE_HTML},
  {"application/xhtml+xml", HTMLPARSER_MODE_HTML},
  {"application/javascript", HTMLPARSER_MODE_JS},
  {"application/json", HTMLPARSER_MODE_JS},
  {"text/javascript", HTMLPARSER_MODE_JS},
  {NULL, -1},
};

/* Characters to escape when html escaping is needed */
static char *HTML_CHARS_LIST    = "&<>\"'\r";

/* Characters to escape when html escaping of an unquoted
   attribute value is needed */
static char *HTML_UNQUOTED_LIST = "&<>\"'\r\n\t ";

/* Characters to escape when javascript escaping is needed */
static char *JS_CHARS_LIST       = "&<>\"'\r\n\t/\\;";

/* Characters to escape when unquoted javascript attribute is escaped */
static char *JS_ATTR_UNQUOTED_LIST = "&<>\"'\r\n\t/\\; ";


/*  The html escaping routine uses this map to lookup the appropriate
 *  entity encoding to use for any metacharacter.
 *  A filler "&nbsp;" has been used for characters that we do not expect to
 *  lookup.
 */
static char* HTML_CHAR_MAP[] = {"&nbsp;", "&nbsp;", "&nbsp;", "&nbsp;", "&nbsp;",
                         "&nbsp;", "&nbsp;", "&nbsp;", "&nbsp;", "&nbsp;",
                         "&nbsp;", "&nbsp;", "&nbsp;", "&nbsp;", "&nbsp;",
                         "&nbsp;", "&nbsp;", "&nbsp;", "&nbsp;", "&nbsp;",
                         "&nbsp;", "&nbsp;", "&nbsp;", "&nbsp;", "&nbsp;",
                         "&nbsp;", "&nbsp;", "&nbsp;", "&nbsp;", "&nbsp;",
                         "&nbsp;", "&nbsp;", "&nbsp;", "!" ,     "&quot;",
                         "#",      "$",      "%",      "&amp;",  "&#39;",
                         "(", ")", "*", "+", ",", "-", ".", "/", "0", "1",
                         "2", "3", "4", "5", "6", "7", "8", "9", ":", ";",
                         "&lt;", "=", "&gt;", "?", "@", "A", "B", "C", "D", "E",
                         "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
                         "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y",
                         "Z", "[", "\\", "]", "^", "_", "`", "a", "b", "c",
                         "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
                         "n", "o", "p", "q", "r", "s", "t", "u", "v", "w",
                         "x", "y", "z", "{", "|", "}", "~", "&nbsp;",};

#define IN_LIST(l, c) ( ((unsigned char)c < 0x80) && (index(l, c) != NULL) )


/* neo_str.c already has all these escaping routines.
 * But since with auto escaping, the escaping routines will be called for every
 * variable, these new functions do an initial pass to determine if escaping is
 * needed. If not, the input string itself is returned, and no extraneous
 * memory allocation or copying is done.
 */

/* TODO(mugdha): Consolidate these functions with the ones in neo_str.c. */

/* Function: neos_auto_html_escape - HTML escapes input if necessary.
 * Description: This function scans through in, looking for HTML metacharacters.
 *              If any metacharacters are found, the input is HTML escaped and
 *              the resulting output returned in *esc.
 * Input: in -> input string
 *        quoted -> should be 0 if the input string appears on an HTML attribute
 *                  and is not quoted.
 * Output: esc -> pointer to output string. Could point back to input string
 *                if the input does not need modification.
 *         do_free -> will be 1 if *esc should be freed. If it is 0, *esc
 *                    points to in.
 */
static NEOERR *neos_auto_html_escape (const char *in, char **esc,
                                      int quoted, int *do_free)
{
  unsigned int extra = 0;
  unsigned int l = 0;
  char *tmp = NULL;
  char *metachars = HTML_CHARS_LIST;

  if (!quoted)
    metachars = HTML_UNQUOTED_LIST;

  /*
     Check if there are any characters that need escaping. In the majority of
     cases, this will be false and we can just quit the function immediately
  */
  while (in[l])
  {
    if (IN_LIST(metachars, in[l]))
    {
      extra += strlen(HTML_CHAR_MAP[(int)in[l]]) - 1;
    }
    l++;
  }

  if (!extra) {
    *esc = (char *)in;
    do_free = 0;
    return STATUS_OK;
  }

  /*
     There are some HTML metacharacters. Allocate a new string and do escaping
     in that string.
  */
  (*esc) = (char *) malloc(l + extra + 1);
  if (*esc == NULL)
    return nerr_raise (NERR_NOMEM, "Unable to allocate memory to escape %s",
                       in);

  tmp = *esc;
  l = 0;
  while (in[l])
  {
    if (IN_LIST(metachars, in[l]))
    {
      strncpy(tmp, HTML_CHAR_MAP[(int)in[l]], strlen(HTML_CHAR_MAP[(int)in[l]]));
      tmp += strlen(HTML_CHAR_MAP[(int)in[l]]);
    }
    else
    {
      *tmp++ = in[l];
    }
    l++;
  }
  (*esc)[l + extra] = '\0';

  *do_free = 1;
  return STATUS_OK;
}

/* Allowed URI schemes, used by neos_auto_url_validate. If a provided URL
 * has any other scheme, it will be rejected.
 */
static char *AUTO_URL_PROTOCOLS[] = {"http://", "https://", "ftp://", "mailto:"};

/* Function: neos_auto_url_validate - Verify that the input is a valid URL.
 * Description: This function verifies that the input starts with one of the
 *              allowed URI schemes, specified in AUTO_URL_PROTOCOLS.
 *              In addition, the input is html escaped.
 * Input: in -> input string
 *        quoted -> should be 0 if the input string appears on an HTML attribute
 *                  and is not quoted. Will be passed through to the html
 *                  escaping function.
 * Output: esc -> pointer to output string. Could point back to input string
 *                if the input does not need modification.
 *         do_free -> will be 1 if *esc should be freed. If it is 0, *esc
 *                    points to in.
 */
static NEOERR *neos_auto_url_validate (const char *in, char **esc,
                                       int quoted, int *do_free)
{
  int valid = 0;
  size_t i;
  size_t inlen;
  int num_protocols = sizeof(AUTO_URL_PROTOCOLS) / sizeof(char*);
  char* slashpos;
  void* colonpos;
  char *s;

  inlen = strlen(in);

  /*
   * <a href="//b:80"> or <a href="a/b:80"> are allowed by browsers
   * and ":" is treated as part of the path, while
   * <a href="www.google.com:80"> is an invalid url
   * and ":" is treated as a scheme separator.
   *
   * Hence allow for ":" in the path part of a url (after /)
   */
  slashpos = strchr(in, '/');
  if (slashpos == NULL) {
    i = inlen;
  }
  else {
    i = (size_t)(slashpos - in);
  }

  colonpos = memchr(in, ':', i);

  if (colonpos == NULL) {
    /* no scheme in 'in': so this is a relative url */
    valid = 1;
  }
  else {
    for (i = 0; i < num_protocols; i++)
    {
      if (strncmp(in, AUTO_URL_PROTOCOLS[i], strlen(AUTO_URL_PROTOCOLS[i]))
          == 0) {
        /* 'in' starts with one of the allowed protocols */
        valid = 1;
        break;
      }

    }
  }

  if (valid)
    return nerr_pass(neos_auto_html_escape(in, esc, quoted, do_free));

  /* 'in' contains an unsupported scheme, replace with '#' */
  s = (char *) malloc(2);
  if (s == NULL)
    return nerr_raise (NERR_NOMEM,
                       "Unable to allocate memory to escape %s", in);

  s[0] = '#';
  s[1] = '\0';
  *esc = s;
  *do_free = 1;
  return STATUS_OK;

}

/* Function: neos_auto_check_number - Verify that in points to a number.
 * Description: This function scans through in and validates that it contains
 *              a number. Digits, decimal points and spaces are ok. If the
 *              input is not a valid number, output is set to '_'. A pointer
 *              to the output is returned in *esc.
 * Input: in -> input string
 *
 * Output: esc -> pointer to output string. Will point back to input string
 *                if the input is a valid number. Otherwise, points to '_'.
 *         do_free -> will be 1 if *esc should be freed. If it is 0, *esc
 *                    points to in.
 */
static NEOERR *neos_auto_check_number (const char *in, char **esc, int *do_free)
{
  int l = 0;
  char *s;

  while (in[l]) {

    if (!isdigit(in[l]) && in[l] != '.' && in[l] != ' ')
      break;
    l++;
  }

  if (in[l]) {
    s = (char *) malloc(2);
    if (s == NULL)
      return nerr_raise (NERR_NOMEM,
                         "Unable to allocate memory to escape %s", in);

    s[0] = '_';
    s[1] = '\0';
    *esc = s;
    *do_free = 1;
  }
  else {
    *esc = (char *)in;
    *do_free = 0;
  }

  return STATUS_OK;
}

/* Function: neos_auto_css_validate - Verify that in points to safe css subset.
 * Description: This function verififes that in points to a safe subset of
 *              characters that are ok to use inside a style attribute.
 *              Alphanumeric characters, spaces and some delimiters are
 *              allowed. Any unsafe characters are stripped out. A pointer to
 *              the output is returned in *esc.
 * Input: in -> input string
 *
 * Output: esc -> pointer to output string. Will point back to input string
 *                if the input is not modified.
 *         do_free -> will be 1 if *esc should be freed. If it is 0, *esc
 *                    points to in.
 */
static NEOERR *neos_auto_css_validate (const char *in, char **esc,
                                       int quoted, int *do_free)
{
  /* TODO(mugdha): This won't work as is for style tags :
                   - no {}, no \r\n
                   - Currently this does not allow html chars. if they should
                     be allowed, they need to be escaped.
  */
  int l = 0;

  while (in[l] &&
         (isalnum(in[l]) || (in[l] == ' ' && quoted) ||
          in[l] == '_' || in[l] == '.' || in[l] == ',' ||
          in[l] == '!' || in[l] == '#' || in[l] == '%' ||
          in[l] == '-' || in[l] == ':' || in[l] == ';' )) {
    l++;
  }

  if (!in[l]) {
    /* while() looped successfully through all characters in 'in'.
       'in' is safe to use as is */
    *esc = (char *)in;
    *do_free = 0;
  }
  else {
    /* Create a new string, stripping out all dangerous characters from 'in' */
    int i = 0;
    char *s;
    s = (char *) malloc(strlen(in) + 1);
    if (s == NULL)
      return nerr_raise (NERR_NOMEM,
                         "Unable to allocate memory to escape %s", in);

    l = 0;
    while (in[l]) {
      /* Strip out all except a whitelist of characters */
      if (isalnum(in[l]) || (in[l] == ' ' && quoted) ||
          in[l] == '_' || in[l] == '.' || in[l] == ',' ||
          in[l] == '!' || in[l] == '#' || in[l] == '%' ||
          in[l] == '-' || in[l] == ':' ) {
        s[i++] = in[l];
      }

      l++;
    }
    s[i] = '\0';
    *esc = s;
    *do_free = 1;
  }

  return STATUS_OK;
}

/* Function: neos_auto_js_escape - Javascript escapes input if necessary.
 * Description: This function scans through in, looking for javascript
 *              metacharacters. If any are found, the input is js escaped and
 *              the resulting output returned in *esc.
 * Input: in -> input string
 *        attr_quoted -> should be 0 if the input string appears on an JS
 *                       attribute and the entire attribute is not quoted.
 * Output: esc -> pointer to output string. Could point back to input string
 *                if the input does not need modification.
 *         do_free -> will be 1 if *esc should be freed. If it is 0, *esc
 *                    points to in.
 */
static NEOERR *neos_auto_js_escape (const char *in, char **esc,
                                    int attr_quoted, int *do_free)
{
  int nl = 0;
  int l = 0;
  char *s;
  char *metachars = JS_CHARS_LIST;

  /*
    attr_quoted can be false if
    - a variable inside a javascript attribute is being escaped
    - AND, the variable itself is quoted, but the entire attribute is not.
    <input onclick=alert('<?cs var: Blah ?>');>
    The variable could be used to inject additional attributes on the tag.
  */
  if (!attr_quoted)
    metachars = JS_ATTR_UNQUOTED_LIST;

  while (in[l])
  {
    if (IN_LIST(metachars, in[l]) || (in[l] > 0 && in[l] < 32))
    {
      nl += 3;
    }
    l++;
  }

  if (nl == 0) {
    *esc = (char *)in;
    do_free = 0;
    return STATUS_OK;
  }

  nl += l;
  s = (char *) malloc(nl + 1);
  if (s == NULL)
    return nerr_raise (NERR_NOMEM, "Unable to allocate memory to escape %s",
                       in);

  l = 0;
  nl = 0;
  while (in[l])
  {
    if (IN_LIST(metachars, in[l]) || (in[l] > 0 && in[l] < 32))
    {
      s[nl++] = '\\';
      s[nl++] = 'x';
      s[nl++] = "0123456789ABCDEF"[(in[l] >> 4) & 0xF];
      s[nl++] = "0123456789ABCDEF"[in[l] & 0xF];
      l++;
    }
    else
    {
      s[nl++] = in[l++];
    }
  }
  s[nl] = '\0';

  *esc = (char *)s;
  return STATUS_OK;
}

NEOERR *neos_auto_escape(NEOS_AUTO_CTX *ctx, const char* str, char **esc,
                         int *do_free)
{
  htmlparser_ctx *hctx = (htmlparser_ctx *)ctx;
  int st;
  const char *tag;

  if (!ctx)
    return nerr_raise(NERR_ASSERT, "ctx is NULL");

  if (!str)
    return nerr_raise(NERR_ASSERT, "str is NULL");

  if (!esc)
    return nerr_raise(NERR_ASSERT, "esc is NULL");

  if (!do_free)
    return nerr_raise(NERR_ASSERT, "do_free is NULL");

  st = htmlparser_state(hctx);
  tag = htmlparser_tag(hctx);

  /* Inside an HTML attribute value */
  if (st == HTMLPARSER_STATE_VALUE) {

    int type = htmlparser_attr_type(hctx);
    int attr_quoted = htmlparser_is_attr_quoted(hctx);

    switch (type) {
      case HTMLPARSER_ATTR_REGULAR:
        /* <input value="<?cs var: Blah ?>"> : */
        return nerr_pass(neos_auto_html_escape(str, esc,
                                               attr_quoted, do_free));
        
      case HTMLPARSER_ATTR_URI:
        if (htmlparser_value_index(hctx) == 0)
          /* <a href="<?cs var:MyUrl ?>"> : Validate URI scheme of MyUrl */
          return nerr_pass(neos_auto_url_validate(str, esc,
                                                  attr_quoted, do_free));
        else
          /* <a href="http://www.blah.com?x=<?cs var: MyQuery ?>">:
            MyQuery is not at start of URL, so it only needs html escaping.
          */
          return nerr_pass(neos_auto_html_escape(str, esc,
                                                 attr_quoted, do_free));

      case HTMLPARSER_ATTR_JS:
        if (htmlparser_is_js_quoted(hctx))
          /* <input onclick="alert('<?cs var:Blah ?>');"> OR
             <input onclick=alert('<?cs var: Blah ?>');>
          */
          /*
            Note: neos_auto_js_escape() hex encodes all html metacharacters.
            Therefore it is safe to not do an HTML escape around this.
          */
          return nerr_pass(neos_auto_js_escape(str, esc, attr_quoted,
                                               do_free));
        else
          /* <input onclick="alert(<?cs var:Blah ?>);"> OR
             <input onclick=alert(<?cs var:Blah ?>);> :
            
            There are no quotes around the variable, it could be used to
            inject arbitrary javascript. Only reason to omit the quotes is if
            the variable is intended to be a number.
          */
          return nerr_pass(neos_auto_check_number(str, esc, do_free));
        break;

      case HTMLPARSER_ATTR_STYLE:
        /* <input style="border:<?cs var: FancyBorder ?>"> : */
        return nerr_pass(neos_auto_css_validate(str, esc,
                                                attr_quoted, do_free));

      default:
        return nerr_raise(NERR_ASSERT, 
                          "Unknown attr type received from HTML parser : %d\n",
                          type);
    }
  }

  if (st == HTMLPARSER_STATE_CSS_FILE || (tag && strcmp(tag, "style") == 0)) {
    /* TODO(mugdha): Validate variables in style tags later */
    *esc = (char *) str;
    *do_free = 0;
    return STATUS_OK;
  }

  /* Inside javascript. Do JS escaping */
  if (htmlparser_in_js(hctx)) {
    if (htmlparser_is_js_quoted(hctx))
      /* <script> var a = <?cs var: Blah ?>; </script> */
      /* TODO(mugdha): This also includes variables inside javascript comments.
         They will also get stripped out if they are not numbers.
      */
      return nerr_pass(neos_auto_js_escape(str, esc, 1, do_free));
    else
      /* <script> var a = "<?cs var: Blah ?>"; </script> */
      return nerr_pass(neos_auto_check_number(str, esc, do_free));
  }

  /* Default is assumed to be HTML body */
  /* <b>Hello <?cs var: UserName ?></b> : */
  return nerr_pass(neos_auto_html_escape(str, esc, 1, do_free));

}

NEOERR *neos_auto_parse(NEOS_AUTO_CTX *ctx, const char *str, int len)
{
  if (!ctx)
    return nerr_raise(NERR_ASSERT, "ctx is NULL");

  if (!str)
    return nerr_raise(NERR_ASSERT, "str is NULL");

  /* TODO(mugdha): Add a check for return value HTMLPARSER_STATE_ERROR when it is
     available */
  htmlparser_parse((htmlparser_ctx*)ctx, str, len);
  return STATUS_OK;
}

NEOERR *neos_auto_set_content_type(NEOS_AUTO_CTX *ctx, const char *type)
{
  struct _neos_content_map *esc;

  if (!ctx)
    return nerr_raise(NERR_ASSERT, "ctx is NULL");

  if (!type)
    return nerr_raise(NERR_ASSERT, "type is NULL");

  for (esc = &ContentTypeList[0]; esc->content_type != NULL; esc++) {
    if (strcmp(type, esc->content_type) == 0) {
        htmlparser_reset_mode((htmlparser_ctx*)ctx, esc->parser_mode);
        return STATUS_OK;
    }
  }

  return nerr_raise(NERR_ASSERT,
                    "unknown content type supplied: %s", type);

}

NEOERR* neos_auto_init(NEOS_AUTO_CTX **pctx)
{
  NEOERR *err = STATUS_OK;

  if (!pctx)
    return nerr_raise(NERR_ASSERT, "pctx is NULL");

  *pctx = htmlparser_new();

  if (*pctx == NULL)
    err = nerr_raise(NERR_NOMEM, "Could not create autoescape context");

  return err;

  /* Note: Calling htmlparser_reset() instead of htmlparser_new() will
     be more efficient. But unclear right now how to achieve this reuse.
  */
}

void neos_auto_destroy(NEOS_AUTO_CTX **pctx)
{
  if (!pctx)
    return;

  if (*pctx) {
    htmlparser_delete((htmlparser_ctx*)*pctx);
  }
  *pctx = NULL;
}
