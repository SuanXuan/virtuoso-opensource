--
--  $Id$
--
--  This file is part of the OpenLink Software Virtuoso Open-Source (VOS)
--  project.
--
--  Copyright (C) 1998-2006 OpenLink Software
--
--  This project is free software; you can redistribute it and/or modify it
--  under the terms of the GNU General Public License as published by the
--  Free Software Foundation; only version 2 of the License, dated June 1991.
--
--  This program is distributed in the hope that it will be useful, but
--  WITHOUT ANY WARRANTY; without even the implied warranty of
--  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
--  General Public License for more details.
--
--  You should have received a copy of the GNU General Public License along
--  with this program; if not, write to the Free Software Foundation, Inc.,
--  51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
--

-- ---------------------------------------------------
-- Polls 'Users' code generation file.
-- ---------------------------------------------------

------------------------------------------------------------------------------
create procedure POLLS.WA.exec_no_error(in expr varchar, in execType varchar := '', in execTable varchar := '', in execColumn varchar := '')
{
  declare
    state,
    message,
    meta,
    result any;

  log_enable(1);
  if (execType = 'C') {
    if (coalesce((select 1 from DB.DBA.SYS_COLS where (0=casemode_strcmp("COLUMN", execColumn)) and (0=casemode_strcmp ("TABLE", execTable))), 0))
      return;
  }
  if (execType = 'D') {
    if (not coalesce((select 1 from DB.DBA.SYS_COLS where (0=casemode_strcmp("COLUMN", execColumn)) and (0=casemode_strcmp ("TABLE", execTable))), 0))
      return;
  }
  if (execType = 'S') {
    declare S varchar;
    declare maxID integer;

    S := sprintf('select max(%s) from %s', execColumn, execTable);
    maxID := 1000;
    state := '00000';

    exec(S, state, message, vector(), 0, meta, result);
    if (state = '00000')
      if (not isnull(result[0][0]))
        maxID := result[0][0] + 1;

    expr := sprintf(expr, maxID);
  }
  exec(expr, state, message, vector(), 0, meta, result);
}
;

------------------------------------------------------------------------------
--
create procedure POLLS.WA.vhost()
{
  declare
    iIsDav integer;
  declare
    sHost varchar;

  -- Add a virtual directory for Polls - public www -------------------------
  sHost := registry_get('_polls_path_');
  if (cast(sHost as varchar) = '0')
    sHost := '/apps/polls/';
  iIsDav := 1;
  if (isnull(strstr(sHost, '/DAV')))
    iIsDav := 0;
  VHOST_REMOVE(lpath    => '/polls');
  VHOST_DEFINE(lpath    => '/polls',
               ppath    => concat(sHost, 'www/'),
               is_dav   => iIsDav,
               is_brws  => 0,
               vsp_user => 'dba',
               realm    => 'wa',
               def_page => 'polls.vspx'
             );
}
;

POLLS.WA.vhost();

-------------------------------------------------------------------------------
--
-- Insert data
--
-------------------------------------------------------------------------------
POLLS.WA.exec_no_error('insert replacing WA_TYPES(WAT_NAME, WAT_TYPE, WAT_REALM, WAT_DESCRIPTION) values (\'Polls\', \'db.dba.wa_polls\', \'wa\', \'Polls Manager Application\')')
;
POLLS.WA.exec_no_error('insert replacing WA_MEMBER_TYPE (WMT_APP, WMT_NAME, WMT_ID, WMT_IS_DEFAULT) values (\'Polls\', \'owner\', 1, 0)')
;
POLLS.WA.exec_no_error('insert replacing WA_MEMBER_TYPE (WMT_APP, WMT_NAME, WMT_ID, WMT_IS_DEFAULT) values (\'Polls\', \'author\', 2, 0)')
;
POLLS.WA.exec_no_error('insert replacing WA_MEMBER_TYPE (WMT_APP, WMT_NAME, WMT_ID, WMT_IS_DEFAULT) values (\'Polls\', \'reader\', 3, 0)')
;

-------------------------------------------------------------------------------
--
-- create new Polls Application in WA
--
-- Polls class
--
POLLS.WA.exec_no_error('
  create type wa_polls under web_app as (
      PollsID varchar,
      owner integer
    )
    constructor method wa_polls(stream any),
    overriding method wa_id_string() returns any,
    overriding method wa_new_inst(login varchar) returns any,
    overriding method wa_front_page(stream any) returns any,
    overriding method wa_state_edit_form(stream any) returns any,
    overriding method wa_home_url() returns varchar,
    overriding method wa_drop_instance() returns any,
    overriding method wa_notify_member_changed(account int, otype int, ntype int, odata any, ndata any, ostatus any, nstatus any) returns any,
    overriding method wa_class_details() returns varchar,
    overriding method wa_front_page_as_user(inout stream any, in user_name varchar) returns any,
    overriding method wa_rdf_url (in vhost varchar, in lhost varchar) returns varchar,
    method wa_dashboard_last_item () returns any,
    method wa_vhost_options () returns any,
    method get_param (in param varchar) returns any
'
)
;

-------------------------------------------------------------------------------
--
-- wa_polls methods
--
-------------------------------------------------------------------------------
--
create constructor method wa_polls (inout stream any) for wa_polls
{
  return;
}
;

-------------------------------------------------------------------------------
--
create method wa_id_string() for wa_polls
{
  return self.PollsID;
}
;

-------------------------------------------------------------------------------
--
create method wa_drop_instance () for wa_polls
{
  POLLS.WA.domain_delete (self.PollsID);
  VHOST_REMOVE(lpath => concat ('/polls/', self.PollsID));
  (self as web_app).wa_drop_instance ();
}
;

-------------------------------------------------------------------------------
--
create method wa_notify_member_changed(in account int, in otype int, in ntype int, in odata any, in ndata any, in ostatus any, in nstatus any) for wa_polls
{
  if (isnull (ntype))
    POLLS.WA.account_delete (self.PollsID, account);
  return (self as web_app).wa_notify_member_changed (account, otype, ntype, odata, ndata, ostatus, nstatus);
}
;

-------------------------------------------------------------------------------
--
-- owner makes a new Polls
--
create method wa_new_inst (in login varchar) for wa_polls
{
  declare
    iUserID,
    iWaiID integer;

  iUserID := (select U_ID from DB.DBA.SYS_USERS where U_NAME = login);
  if (isnull(iUserID))
    signal('EN001', 'not a Virtuoso WA user');

  if (self.wa_member_model is null)
    self.wa_member_model := 0;

  self.owner := iUserID;

  insert into WA_INSTANCE (WAI_NAME, WAI_TYPE_NAME, WAI_INST, WAI_DESCRIPTION)
    values (self.wa_name, 'Polls', self, 'Description');

  select WAI_ID into iWaiID from WA_INSTANCE where WAI_NAME = self.wa_name;
  self.PollsID := cast(iWaiID as varchar);
  update WA_INSTANCE set WAI_INST = self where WAI_ID = iWaiID;

  declare path varchar;
  path := sprintf ('/DAV/home/%s/Polls/', login);
  DB.DBA.DAV_MAKE_DIR (path, iUserID, null, '110100000N');

  -- Add a virtual directory for Polls - public www -------------------------
  VHOST_REMOVE(lpath    => concat('/polls/', self.PollsID));
  VHOST_DEFINE(lpath    => concat('/polls/', self.PollsID),
               ppath    => concat(self.get_param('host'), 'www/'),
               ses_vars => 1,
               is_dav   => self.get_param('isDAV'),
               is_brws  => 0,
               vsp_user => 'dba',
               realm    => 'wa',
               def_page => 'polls.vspx',
               opts     => vector ('domain', self.PollsID)
             );

  POLLS.WA.domain_update (iWaiID, iUserID);
  return (self as web_app).wa_new_inst(login);
}
;

-------------------------------------------------------------------------------
--
create method wa_class_details() for wa_polls
{
  return 'The Virtuoso Polls Application allows you to create and maintain polls.';
}
;

-------------------------------------------------------------------------------
--
create method wa_front_page(inout stream any) for wa_polls
{
  declare sSid varchar;

  sSid := (select VS_SID from VSPX_SESSION where VS_REALM = 'wa' and VS_UID = connection_get('vspx_user'));
  http_request_status ('HTTP/1.1 302 Found');
  http_header(sprintf('Location: %s?sid=%s&realm=%s\r\n', self.wa_home_url(), sSid, 'wa'));
  return;
}
;

-------------------------------------------------------------------------------
--
create method wa_state_edit_form(inout stream any) for wa_polls
{
  declare sSid varchar;

  if (exists(select 1
               from SYS_USERS A,
                    WA_MEMBER B
              where A.U_NAME = connection_get('vspx_user')
                and B.WAM_USER = A.U_ID
                and B.WAM_INST= self.wa_name
                and B.WAM_MEMBER_TYPE = 1))
  {
    sSid := (select VS_SID from VSPX_SESSION where VS_REALM = 'wa' and VS_UID = connection_get('vspx_user'));
    http_request_status ('HTTP/1.1 302 Found');
    http_header(sprintf('Location: %s?sid=%s&realm=%s\r\n', WS.WS.EXPAND_URL(self.wa_home_url(), 'settings.vspx'), sSid, 'wa'));
  } else {
    signal('42001', 'Not a owner');
  }
  return;
}
;

-------------------------------------------------------------------------------
--
create method wa_home_url () for wa_polls
{
  return concat('/polls/', self.PollsID, '/polls.vspx');
}
;

-------------------------------------------------------------------------------
--
create method wa_front_page_as_user (inout stream any, in user_name varchar) for wa_polls
{
  declare sSid, sOwner varchar;

  sOwner := (select TOP 1 U_NAME from SYS_USERS A, WA_MEMBER B where B.WAM_USER = A.U_ID and B.WAM_INST= self.wa_name and B.WAM_MEMBER_TYPE = 1);
  sSid := md5 (concat (datestring (now ()), http_client_ip (), http_path ()));
  insert into DB.DBA.VSPX_SESSION (VS_REALM, VS_SID, VS_UID, VS_STATE, VS_EXPIRY)
    values ('wa', sSid, sOwner, serialize ( vector ('vspx_user', user_name, 'owner_user', sOwner)), now());
  http_request_status ('HTTP/1.1 302 Found');
  http_header(sprintf('Location: %s?sid=%s&realm=%s\r\n', self.wa_home_url(), sSid, 'wa'));
  return;
}
;

-------------------------------------------------------------------------------
--
create method wa_vhost_options () for wa_polls
{
  return vector (
           self.get_param('host') || 'www/', -- physical home
           'polls.vspx',                 -- default page
           'dba',                            -- user for execution
           0,                                -- directory browsing enabled (flag 0/1)
           self.get_param('isDAV'),          -- WebDAV repository  (flag 0/1)
           vector ('domain', self.PollsID),  -- virtual directory options, empty is not applicable
           null,                             -- post-processing function (null is not applicable)
           null                              -- pre-processing (authentication) function
         );
}
;

-------------------------------------------------------------------------------
--
create method get_param (in param varchar) for wa_polls
{
  declare retValue any;

  retValue := null;
  if (param = 'host') {
    retValue := registry_get('_polls_path_');
    if (cast(retValue as varchar) = '0')
      retValue := '/apps/polls/';
  } if (param = 'isDAV') {
    retValue := 1;
    if (isnull(strstr(self.get_param('host'), '/DAV')))
      retValue := 0;
  }
  return retValue;
}
;

-------------------------------------------------------------------------------
--
create method wa_dashboard_last_item () for wa_polls
{
  declare domainID, userID integer;

  domainID := (select WAI_ID from DB.DBA.WA_INSTANCE where WAI_NAME = self.wa_name);
  userID := (select WAM_USER from WA_MEMBER B where WAM_INST = self.wa_name and WAM_MEMBER_TYPE = 1);
  return POLLS.WA.dashboard_get (domainID, userID);
}
;

-------------------------------------------------------------------------------
--
create method wa_rdf_url (in vhost varchar, in lhost varchar) for wa_polls
{
  declare domainID, userID integer;

  domainID := (select WAI_ID from DB.DBA.WA_INSTANCE where WAI_NAME = self.wa_name);
  userID := (select WAM_USER from WA_MEMBER B where WAM_INST= self.wa_name and WAM_MEMBER_TYPE = 1);

  return concat(POLLS.WA.dav_url2(domainID, userID), 'Polls.rdf');
}
;
