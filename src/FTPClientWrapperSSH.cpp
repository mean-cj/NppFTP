/*
    NppFTP: FTP/SFTP functionality for Notepad++
    Copyright (C) 2010  Harry (harrybharry@users.sourceforge.net)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "StdInc.h"
#include "FTPClientWrapper.h"

#include "MessageDialog.h"
#include <fcntl.h>

extern TCHAR * _HostsFile;

FTPClientWrapperSSH::FTPClientWrapperSSH(const char * host, int port, const char * user, const char * password) :
	FTPClientWrapper(Client_SSH, host, port, user, password)
{
}

FTPClientWrapperSSH::~FTPClientWrapperSSH() {
}

FTPClientWrapper* FTPClientWrapperSSH::Clone() {
	FTPClientWrapperSSH* wrapper = new FTPClientWrapperSSH(m_hostname, m_port, m_username, m_password);
	wrapper->SetTimeout(m_timeout);
	wrapper->SetProgressMonitor(m_progmon);
	wrapper->SetCertificates(m_certificates);

	return wrapper;
}

int FTPClientWrapperSSH::Connect() {
	if (m_connected)
		return 0;

	int retcode = connect_ssh();

	if (retcode == 0)
		m_connected = true;

	return OnReturn(retcode);
}

int FTPClientWrapperSSH::Disconnect() {
	if (!m_connected)
		return 0;

	disconnect();

	m_connected = false;

	return OnReturn(0);
}

int FTPClientWrapperSSH::GetDir(const char * path, FTPFile** files) {
	sftp_dir dir;
	sftp_attributes sfile;
	FTPFile file;
	std::vector<FTPFile> vFiles;
	int count = 0;

	dir = sftp_opendir(m_sftpsession, path);
	if(!dir) {
		OutErr("[SFTP] Directory not opened(%s)\n", ssh_get_error(m_sshsession));
		return OnReturn(-1);
	}

	if (m_aborting) {
		sftp_closedir(dir);
		return OnReturn(-1);
	}

	bool endslash = path[strlen(path)-1] == '/';

	/* reading the whole directory, file by file */
	while((sfile = sftp_readdir(m_sftpsession, dir)) && !m_aborting) {
		file.fileName[0] = 0;
		file.filePath[0] = 0;

		if (!strcmp(sfile->name, ".") || !strcmp(sfile->name, ".."))
			continue;

		strcpy(file.filePath, path);
		if (!endslash) {
			strcat(file.filePath, "/");
		}
		strcat(file.filePath, sfile->name);
		strcpy(file.fileName, sfile->name);
		file.fileSize = (long)sfile->size;

		file.mtime = ConvertFiletime(sfile->mtime, sfile->mtime_nseconds);
		file.atime = ConvertFiletime(sfile->atime, sfile->atime_nseconds);
		if (sfile->createtime != 0)
			file.ctime = ConvertFiletime(sfile->createtime, sfile->createtime_nseconds);
		else
			file.ctime = ConvertFiletime(sfile->atime, sfile->atime_nseconds);

		switch(sfile->type) {
			case SSH_FILEXFER_TYPE_DIRECTORY:
				file.fileType = FTPTypeDir;
				break;
			case SSH_FILEXFER_TYPE_REGULAR:
				file.fileType = FTPTypeFile;
				break;
			case SSH_FILEXFER_TYPE_SYMLINK:
				file.fileType = FTPTypeLink;
				break;
		}

		vFiles.push_back(file);
		count++;
	}

	if (m_aborting) {
		sftp_closedir(dir);
		return OnReturn(-1);
	}

	/* when file=NULL, an error has occured OR the directory listing is end of file */
	if(!sftp_dir_eof(dir)){
		OutErr("[SFTP] Unexpected end of directory list: %s\n", ssh_get_error(m_sshsession));
		if (count == 0)
			return OnReturn(-1);
	}

	if(sftp_closedir(dir)){
		OutErr("[SFTP] Unable to close directory: %s\n", ssh_get_error(m_sshsession));
	}

	FTPFile * arrayFiles = new FTPFile[count];
	for(int i = 0; i < count; i++) {
		arrayFiles[i] = vFiles[i];
	}

	*files = arrayFiles;
	return OnReturn(count);
}

int FTPClientWrapperSSH::Cwd(const char * path) {
	sftp_dir dir = sftp_opendir(m_sftpsession, path);
	if(!dir) {
		OutErr("[SFTP] Cannot Cwd to %s(%s)\n", path, ssh_get_error(m_sshsession));
		return OnReturn(-1);
	}

	sftp_closedir(dir);
	return OnReturn(0);
}

int FTPClientWrapperSSH::Pwd(char* buf, size_t size) {
	char * sftppath = sftp_canonicalize_path(m_sftpsession, ".");
	if (!sftppath)
		return OnReturn(-1);

	size_t len = strlen(sftppath);
	if (len > (size-1)) {
		free(sftppath);
		return OnReturn(-1);
	}

	strncpy(buf, sftppath, size);
	if (len > 1 && buf[len-1] == '/')
		buf[len-1] = 0;

	free(sftppath);

	return OnReturn(0);
}

int FTPClientWrapperSSH::Rename(const char * from, const char * to) {
	int retcode = sftp_rename(m_sftpsession, from, to);

	return OnReturn((retcode == 0)?0:-1);
}

int FTPClientWrapperSSH::MkDir(const char * path) {
	int retcode = sftp_mkdir(m_sftpsession, path, 0775);	//default rwxrwxr-x permission

	return OnReturn((retcode == 0)?0:-1);
}

int FTPClientWrapperSSH::RmDir(const char * path) {
	int retcode = sftp_rmdir(m_sftpsession, path);

	return OnReturn((retcode == 0)?0:-1);
}


int FTPClientWrapperSSH::MkFile(const char * path) {
	sftp_file file = sftp_open(m_sftpsession, path, (O_WRONLY|O_CREAT|O_EXCL), 0664);	//default rw-rw-r-- permission
	if (file == NULL)
		return OnReturn(-1);

	//int retcode = sftp_write(file, "", 0);
	sftp_close(file);

	return OnReturn(0);
}

int FTPClientWrapperSSH::SendFile(const TCHAR * localfile, const char * ftpfile) {
	int retcode = 0;
	int res = TRUE;
	HANDLE hFile = INVALID_HANDLE_VALUE;
	sftp_file sfile = NULL;
	const int bufsize = 4096;
	char buf[bufsize];
	DWORD len = 0;
	long totalSent = 0;
	long totalSize = -1;

	hFile = OpenFile(localfile, false);
	if (hFile == INVALID_HANDLE_VALUE)
		return OnReturn(-1);

	DWORD highsize;
	DWORD lowsize = ::GetFileSize(hFile, &highsize);
	//totalSize = ((long)highsize)<<32;
	//totalSize |= lowsize;
	totalSize = lowsize;

	sfile = sftp_open(m_sftpsession, ftpfile, (O_WRONLY|O_CREAT|O_TRUNC), 0664);	//default rw-rw-r-- permission
	if (sfile == NULL) {
		CloseHandle(hFile);
		return OnReturn(-1);
	}

	if (m_aborting) {
		CloseHandle(hFile);
		sftp_close(sfile);
		return OnReturn(-1);
	}

	res = ReadFile(hFile, buf, bufsize, &len, NULL);
	while(res == TRUE && len > 0 && !m_aborting) {
		retcode = sftp_write(sfile, buf, len);
		if (retcode <= 0)
			break;

		totalSent += len;

		if (m_progmon)
			m_progmon->OnDataSent(totalSent, totalSize);

		res = ReadFile(hFile, buf, bufsize, &len, NULL);
	}

	sftp_close(sfile);
	CloseHandle(hFile);

	return OnReturn((res == FALSE || retcode < 0 || m_aborting)?-1:0);
}

int FTPClientWrapperSSH::ReceiveFile(const TCHAR * localfile, const char * ftpfile) {
	int retcode = 0;
	int res = TRUE;
	HANDLE hFile = INVALID_HANDLE_VALUE;
	sftp_file sfile = NULL;
	const int bufsize = 4096;
	char buf[bufsize];
	DWORD len = 0;
	long totalReceived = 0;
	long totalSize = -1;

	sfile = sftp_open(m_sftpsession, ftpfile, (O_RDONLY), 0664);	//default rw-rw-r-- permission
	if (sfile == NULL) {
		OutErr("[SFTP] File not opened %s (%s)\n", ftpfile, ssh_get_error(m_sshsession));
		return OnReturn(-1);
	}

	sftp_attributes fattr = sftp_stat(m_sftpsession, ftpfile);
	if (fattr == NULL) {
		totalSize = -1;
	} else {
		totalSize = (long)fattr->size;
		sftp_attributes_free(fattr);
	}

	hFile = OpenFile(localfile, true);
	if (hFile == INVALID_HANDLE_VALUE || m_aborting) {
		sftp_close(sfile);
		return OnReturn(-1);
	}

	if (m_aborting) {
		CloseHandle(hFile);
		sftp_close(sfile);
		return OnReturn(-1);
	}

	retcode = sftp_read(sfile, buf, bufsize);
	while(retcode > 0 && !m_aborting) {
		res = WriteFile(hFile, buf, retcode, &len, NULL);
		if (res == FALSE)
			break;

		totalReceived += len;

		if (m_progmon)
			m_progmon->OnDataReceived(totalReceived, totalSize);

		retcode = sftp_read(sfile, buf, bufsize);
	}

	sftp_close(sfile);
	CloseHandle(hFile);

	return OnReturn((res == FALSE || retcode < 0 || m_aborting)?-1:0);
}

int FTPClientWrapperSSH::DeleteFile(const char * path) {
	int retcode = sftp_unlink(m_sftpsession, path);

	return (retcode == 0)?0:-1;
}

bool FTPClientWrapperSSH::IsConnected() {
	if (!m_connected)
		return false;

	char * sftppath = sftp_canonicalize_path(m_sftpsession, ".");
	if (!sftppath) {
		//the connection is probably not available.
		//Can test for it by checking the error, but if this fails, what else to do?
		Disconnect();	//thought to be connected, but not anymore, disconnect
		return false;
	}
	free(sftppath);

	return true;
}

//////////////////////////////////////////////////
//////////////////////////////////////////////////
//////////////////////////////////////////////////

int FTPClientWrapperSSH::connect_ssh() {
	ssh_session session;
	int auth = 0;
	int verbosity = 0;

	session=ssh_new();
	if (session == NULL) {
		return -1;
	}

	if (_HostsFile) {
		if (ssh_options_set(session, SSH_OPTIONS_KNOWNHOSTS, _HostsFile) < 0) {
			ssh_free(session);
			return -1;
		}
	}

	if (ssh_options_set(session, SSH_OPTIONS_USER, m_username) < 0) {
		ssh_free(session);
		return -1;
	}

	if (ssh_options_set(session, SSH_OPTIONS_HOST, m_hostname) < 0) {
		ssh_free(session);
		return -1;
	}

	if (ssh_options_set(session, SSH_OPTIONS_PORT, &m_port) < 0) {
		ssh_free(session);
		return -1;
	}

	ssh_options_set(session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
	ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &m_timeout);

	if(ssh_connect(session)) {
		OutErr("[SFTP] Connection failed : %s\n",ssh_get_error(session));
		ssh_disconnect(session);
		ssh_free(session);
		return -1;
	}

	if(verify_knownhost(session) < 0) {
		ssh_disconnect(session);
		ssh_free(session);
		return -1;
	}

	auth = authenticate_password(session);
	if(auth == -1) {
		ssh_disconnect(session);
		ssh_free(session);
		return -1;
	}

	sftp_session sftp = sftp_new(session);

	if(!sftp) {
		OutErr("[SFTP] Error initialising channel: %s\n",ssh_get_error(session));
		ssh_disconnect(session);
		ssh_free(session);
		return -1;
	}

	if(sftp_init(sftp)) {
		OutErr("[SFTP] Error initialising sftp: %s\n",ssh_get_error(session));
		sftp_free(sftp);
		ssh_disconnect(session);
		ssh_free(session);
		return -1;
	}

	m_sshsession = session;
	m_sftpsession = sftp;

	return 0;
}

int FTPClientWrapperSSH::authenticate_password(ssh_session session) {
	int rc;
	int method;
	char *banner;

	// Try to authenticate
	rc = ssh_userauth_none(session, NULL);
	if (rc == SSH_AUTH_ERROR) {
		OutErr("[SFTP] Authentication failed.");
		return -1;
	}

	method = ssh_auth_list(session);
	// Try to authenticate with password
	if (method & SSH_AUTH_METHOD_PASSWORD) {
		rc = ssh_userauth_password(session, NULL, m_password);
		if (rc == SSH_AUTH_ERROR) {
			OutErr("[SFTP] Authentication failed.");
			return -1;
		}
	} else {
		OutErr("[SFTP] No password authentication.");
		return -1;
	}

	banner = ssh_get_issue_banner(session);
	if (banner) {
		OutMsg("[SFTP] Banner: %s\n", banner);
		free(banner);
	}

	return 0;
}

int FTPClientWrapperSSH::verify_knownhost(ssh_session session) {
	int state;
	int result = -1;
	unsigned char *hash = NULL;
	char * hashHex;
	int hlen;
	TCHAR errMessage[512];

	state = ssh_is_server_known(session);

	hlen = ssh_get_pubkey_hash(session, &hash);
	if (hlen < 0) {
		return -1;
	}
	hashHex = ssh_get_hexa(hash, hlen);

	switch(state){
		case SSH_SERVER_KNOWN_OK:
			OutMsg("[SFTP] Host key accepted");
			result = 0;
			break; /* ok */
		case SSH_SERVER_KNOWN_CHANGED:
			OutErr("[SFTP] The host key had changed, and is now: %s", hashHex);
			OutErr("For security reasons, connection will be stopped.");
			result = -1;
			break;
		case SSH_SERVER_FOUND_OTHER:
			OutErr("[SFTP] The host key for this server was not found but an other type of key exists.");
			OutErr("For security reasons, connection will be stopped.");
			result = -1;
			break;
		case SSH_SERVER_FILE_NOT_FOUND:
			OutMsg("[SFTP] Creating known hosts file.");
			/* fallback to SSH_SERVER_NOT_KNOWN behavior */
		case SSH_SERVER_NOT_KNOWN: {
			//MessageDialog md;
			SU::TSprintf(errMessage, 512, TEXT("The server is unknown. Do you trust the host key\r\n%s ?"), hashHex);
			//int res = md.Create(_MainOutputWindow, TEXT("SFTP authentication"), errMessage);
			int res = ::MessageBox(_MainOutputWindow, errMessage, TEXT("SFTP authentication"), MB_YESNO);
			if (res == IDYES) {
				if (ssh_write_knownhost(session) < 0) {
					OutErr("[SFTP] Writing known hosts file failed: %s", strerror(errno));
					result = -1;
				}
				OutMsg("[SFTP] Host key written to file");
				result = 0;
			} else {
				OutErr("[SFTP] Rejected host key");
				result = -1;
			}

			break; }
		case SSH_SERVER_ERROR:
		default:
			OutErr("[SFTP] SSH_SERVER_ERROR: %s",ssh_get_error(session));
			result = -1;
			break;
	}

	free(hash);
	free(hashHex);

	return result;
}

int FTPClientWrapperSSH::disconnect() {
	sftp_free(m_sftpsession);
	ssh_disconnect(m_sshsession);
	ssh_free(m_sshsession);

	m_sftpsession = NULL;
	m_sshsession = NULL;

	return 0;
}

HANDLE FTPClientWrapperSSH::OpenFile(const TCHAR* file, bool write) {
	HANDLE hFile = INVALID_HANDLE_VALUE;

	DWORD access;
	DWORD share;
	DWORD creation;

	if (write) {
		access = GENERIC_WRITE;
		share = FILE_SHARE_READ;
		creation = CREATE_ALWAYS;
		int res = PU::CreateLocalDirFile(file);
		if (res == -1)
			return hFile;
	} else {
		access = GENERIC_READ;
		share = FILE_SHARE_READ;
		creation = OPEN_EXISTING;
	}


	hFile = ::CreateFile(file, access, share, NULL, creation, 0, NULL);

	return hFile;
}

FILETIME FTPClientWrapperSSH::ConvertFiletime(uint32_t nTime, uint32_t /*nNanosecs*/) {
	FILETIME ft;
	// Note that LONGLONG is a 64-bit value
	LONGLONG ll;

	ll = Int32x32To64(nTime, 10000000);// + ((LONGLONG)116444736000000000);
	ll += Int32x32To64(116444736, 1000000000);
	ft.dwLowDateTime = (DWORD)ll;
	ft.dwHighDateTime = ll >> 32;

	return ft;
}