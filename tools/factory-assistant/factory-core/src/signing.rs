//! 100ask device_sig: ECDSA P-256 + SHA-256 over UTF-8 `100ask:<cpuid>`.

use p256::ecdsa::signature::{DigestSigner, DigestVerifier};
use p256::ecdsa::{Signature, SigningKey, VerifyingKey};
use p256::pkcs8::{DecodePrivateKey, DecodePublicKey};
use p256::SecretKey;
use sha2::{Digest, Sha256};
use std::path::Path;
use std::process::Command;

pub fn normalize_cpuid(raw: &str) -> Result<String, String> {
    let cpuid = raw.trim().to_ascii_lowercase();
    if cpuid.len() == 32 && cpuid.chars().all(|c| c.is_ascii_hexdigit()) {
        Ok(cpuid)
    } else {
        Err("CPUID 必须是 32 位小写十六进制".to_string())
    }
}

fn extract_pem_blocks(pem: &str) -> Vec<String> {
    let mut blocks = Vec::new();
    let mut current = String::new();
    let mut in_block = false;
    for line in pem.lines() {
        if line.starts_with("-----BEGIN ") {
            in_block = true;
            current.clear();
            current.push_str(line);
            current.push('\n');
            continue;
        }
        if in_block {
            current.push_str(line);
            current.push('\n');
            if line.starts_with("-----END ") {
                blocks.push(current.clone());
                in_block = false;
            }
        }
    }
    blocks
}

fn load_signing_key(pem: &str) -> Result<SigningKey, String> {
    // Prefer PRIVATE KEY blocks; skip EC PARAMETERS.
    let blocks = extract_pem_blocks(pem);
    let candidates: Vec<&str> = if blocks.is_empty() {
        vec![pem]
    } else {
        blocks
            .iter()
            .filter(|b| b.contains("PRIVATE KEY"))
            .map(String::as_str)
            .collect()
    };
    let try_list = if candidates.is_empty() {
        vec![pem]
    } else {
        candidates
    };

    let mut errors = Vec::new();
    for block in try_list {
        match SigningKey::from_pkcs8_pem(block) {
            Ok(k) => return Ok(k),
            Err(e) => errors.push(format!("pkcs8: {e}")),
        }
        match SecretKey::from_sec1_pem(block) {
            Ok(sk) => return Ok(SigningKey::from(sk)),
            Err(e) => errors.push(format!("sec1: {e}")),
        }
    }
    Err(format!(
        "无法解析 100ask 私钥 PEM（支持 PKCS#8 / EC PRIVATE KEY）: {}",
        errors.join(" | ")
    ))
}

fn sign_message_hex_rust(private_key_pem: &str, message: &[u8]) -> Result<String, String> {
    let signing_key = load_signing_key(private_key_pem)?;
    let mut hasher = Sha256::new();
    hasher.update(message);
    let signature: Signature = signing_key.sign_digest(hasher);
    let der = signature.to_der();
    Ok(hex::encode(der.as_bytes()))
}

fn sign_message_hex_openssl(private_key_path: &Path, message: &[u8]) -> Result<String, String> {
    let tmp_msg = std::env::temp_dir().join(format!(
        "aitvbox_sign_msg_{}.bin",
        std::process::id()
    ));
    let tmp_sig = std::env::temp_dir().join(format!(
        "aitvbox_sign_sig_{}.bin",
        std::process::id()
    ));
    std::fs::write(&tmp_msg, message).map_err(|e| e.to_string())?;
    let status = Command::new("openssl")
        .args([
            "dgst",
            "-sha256",
            "-sign",
            private_key_path
                .to_str()
                .ok_or_else(|| "私钥路径非 UTF-8".to_string())?,
            "-out",
            tmp_sig.to_str().unwrap_or("sig.bin"),
            tmp_msg.to_str().unwrap_or("msg.bin"),
        ])
        .status()
        .map_err(|e| format!("调用 openssl 失败: {e}"))?;
    let _ = std::fs::remove_file(&tmp_msg);
    if !status.success() {
        let _ = std::fs::remove_file(&tmp_sig);
        return Err("openssl dgst -sign 失败".to_string());
    }
    let der = std::fs::read(&tmp_sig).map_err(|e| e.to_string())?;
    let _ = std::fs::remove_file(&tmp_sig);
    Ok(hex::encode(der))
}

pub fn sign_cpuid_file(private_key_path: &Path, cpuid: &str) -> Result<String, String> {
    let cpuid = normalize_cpuid(cpuid)?;
    let pem = std::fs::read_to_string(private_key_path)
        .map_err(|e| format!("读取私钥失败 {}: {e}", private_key_path.display()))?;
    let message = format!("100ask:{cpuid}");
    let sig = match sign_message_hex_rust(&pem, message.as_bytes()) {
        Ok(s) => s,
        Err(rust_err) => match sign_message_hex_openssl(private_key_path, message.as_bytes()) {
            Ok(s) => s,
            Err(openssl_err) => {
                return Err(format!("{rust_err}; openssl 回退也失败: {openssl_err}"))
            }
        },
    };
    if sig.len() < 128 || sig.len() > 160 || sig.len() % 2 != 0 {
        return Err(format!("签名长度异常: {} hex 字符", sig.len()));
    }
    if !sig.chars().all(|c| c.is_ascii_hexdigit()) {
        return Err("签名不是合法十六进制".to_string());
    }
    Ok(sig)
}

pub fn verify_cpuid_sig(public_key_pem: &str, cpuid: &str, sig_hex: &str) -> Result<bool, String> {
    let cpuid = normalize_cpuid(cpuid)?;
    // Public key file may also have extra blocks; take first PUBLIC KEY
    let pem = extract_pem_blocks(public_key_pem)
        .into_iter()
        .find(|b| b.contains("PUBLIC KEY"))
        .unwrap_or_else(|| public_key_pem.to_string());
    let verifying_key = VerifyingKey::from_public_key_pem(&pem)
        .map_err(|e| format!("无法解析公钥 PEM: {e}"))?;
    let der = hex::decode(sig_hex.trim()).map_err(|e| format!("签名 hex 解码失败: {e}"))?;
    let signature = Signature::from_der(&der).map_err(|e| format!("签名 DER 无效: {e}"))?;
    let message = format!("100ask:{cpuid}");
    let mut hasher = Sha256::new();
    hasher.update(message.as_bytes());
    Ok(verifying_key.verify_digest(hasher, &signature).is_ok())
}

pub fn sha256_hex(data: &[u8]) -> String {
    let mut hasher = Sha256::new();
    hasher.update(data);
    hex::encode(hasher.finalize())
}
