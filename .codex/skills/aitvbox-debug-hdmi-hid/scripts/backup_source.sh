#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: backup_source.sh REPOSITORY [BACKUP_ROOT]" >&2
}

repository_input="${1:-}"
backup_root_input="${2:-}"
if [[ -z "${repository_input}" || $# -gt 2 ]]; then
    usage
    exit 2
fi

repository="$(realpath "${repository_input}")"
if [[ ! -d "${repository}/.git" ]]; then
    echo "not a Git worktree: ${repository}" >&2
    exit 2
fi

repository_name="$(basename "${repository}")"
repository_parent="$(dirname "${repository}")"
if [[ -n "${backup_root_input}" ]]; then
    backup_root="$(realpath -m "${backup_root_input}")"
else
    backup_root="${repository_parent}/${repository_name}-backups"
fi

case "${backup_root}/" in
    "${repository}/"*)
        echo "backup root must be outside the repository: ${backup_root}" >&2
        exit 2
        ;;
esac

timestamp="$(date +%Y%m%dT%H%M%S%z)"
destination="${backup_root}/${timestamp}"
archive="${destination}/${repository_name}-complete-working-tree.tar.gz"
bundle="${destination}/${repository_name}-git-history.bundle"

mkdir -p "${destination}"
git -C "${repository}" bundle create "${bundle}" --all
tar -czpf "${archive}" -C "${repository_parent}" "${repository_name}"

tar -tzf "${archive}" >/dev/null
git bundle verify "${bundle}"
sha256sum "${archive}" "${bundle}"
ls -lh "${archive}" "${bundle}"
printf 'backup_directory=%s\n' "${destination}"
