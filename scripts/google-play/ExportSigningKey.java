import com.google.wireless.android.vending.developer.signing.tools.extern.export.ExportEncryptedPrivateKeyTool;
import com.google.wireless.android.vending.developer.signing.tools.extern.export.KeystoreKey;
import org.bouncycastle.jce.provider.BouncyCastleProvider;

import java.io.ByteArrayInputStream;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermission;
import java.security.KeyStore;
import java.security.MessageDigest;
import java.security.Security;
import java.security.cert.Certificate;
import java.security.cert.CertificateFactory;
import java.util.Arrays;
import java.util.Collection;
import java.util.HashSet;
import java.util.HexFormat;
import java.util.Optional;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/** Invokes the pinned, unmodified Google PEPK implementation without secret argv. */
public final class ExportSigningKey {
    private static final String EXPECTED_CERTIFICATE_SHA256 =
            "d1162e4a7a9bb802be08948ca33c90da61f0084fac7250cb981a7da9fc26f24d";
    private static final String EXPECTED_PEPK_SHA256 =
            "aaccc0774b240aa5304bdad2a49865e92f229ca73209ecb6eaafe75dc858e24e";
    private static final String EXPECTED_ENCRYPTION_PEM_SHA256 =
            "7c50d42784451af7b1f437a6298341c1f866a3dbab665f54a376e2d6c6029264";
    private static final Set<String> EXPECTED_ZIP_ENTRIES =
            Set.of("encryptedPrivateKey", "certificate.pem");
    private static final int MAX_ENTRY_BYTES = 64 * 1024;

    private ExportSigningKey() {}

    public static void main(String[] args) {
        char[] storePassword = null;
        char[] keyPassword = null;
        Path output = null;
        boolean exportStarted = false;
        boolean complete = false;
        String report = null;
        try {
            require(args.length == 3, "Expected keystore, encryption-public-key and output paths");
            Path keystore = Path.of(args[0]);
            Path publicKey = Path.of(args[1]);
            output = Path.of(args[2]);
            requireRegularFile(keystore);
            requireRegularFile(publicKey);
            require(!Files.exists(output, LinkOption.NOFOLLOW_LINKS), "Output already exists");

            // Both inputs were obtained from this application's authenticated Play Console.
            Path pepk = Path.of(ExportEncryptedPrivateKeyTool.class.getProtectionDomain()
                    .getCodeSource().getLocation().toURI());
            requireRegularFile(pepk);
            require(sha256(pepk).equals(EXPECTED_PEPK_SHA256), "Unexpected PEPK implementation");
            require(sha256(publicKey).equals(EXPECTED_ENCRYPTION_PEM_SHA256),
                    "Unexpected Google encryption public key");

            String alias = requiredEnv("ANDROID_RELEASE_KEY_ALIAS");
            storePassword = requiredEnv("ANDROID_RELEASE_STORE_PASSWORD").toCharArray();
            keyPassword = requiredEnv("ANDROID_RELEASE_KEY_PASSWORD").toCharArray();
            KeyStore store = KeyStore.getInstance("JKS");
            try (InputStream input = Files.newInputStream(keystore)) {
                store.load(input, storePassword);
            }
            require(store.isKeyEntry(alias), "Expected private-key entry is absent");
            Certificate certificate = store.getCertificate(alias);
            require(certificate != null, "Signing certificate is absent");
            byte[] certificateDer = certificate.getEncoded();
            require(sha256(certificateDer).equals(EXPECTED_CERTIFICATE_SHA256),
                    "Unexpected app signing certificate");

            // PEPK.main prints argv on parse errors and prompts through System.console().
            // Its public API accepts password char[] directly, avoiding both code paths.
            Security.addProvider(new BouncyCastleProvider());
            KeystoreKey key = new KeystoreKey(keystore, alias, storePassword, keyPassword);
            exportStarted = true;
            new ExportEncryptedPrivateKeyTool().run(
                    true, publicKey.toString(), output.toString(), key, Optional.empty(), true);
            Files.setPosixFilePermissions(output,
                    Set.of(PosixFilePermission.OWNER_READ, PosixFilePermission.OWNER_WRITE));
            validateZip(output, certificateDer);

            report = "{\"schemaVersion\":1,\"status\":\"encrypted_export_validated\","
                    + "\"certificateSha256\":\"" + EXPECTED_CERTIFICATE_SHA256 + "\","
                    + "\"pepkJarSha256\":\"" + EXPECTED_PEPK_SHA256 + "\","
                    + "\"encryptionPublicKeyFileSha256\":\""
                    + EXPECTED_ENCRYPTION_PEM_SHA256 + "\","
                    + "\"encryptedZipSha256\":\"" + sha256(output) + "\","
                    + "\"encryptedZipBytes\":" + Files.size(output) + ","
                    + "\"zipEntries\":[\"encryptedPrivateKey\",\"certificate.pem\"]}";
            complete = true;
        } catch (Exception failure) {
            // Do not print messages, stack traces, argv or environment containing secrets.
            System.err.println("Encrypted signing-key export failed ("
                    + failure.getClass().getSimpleName() + ")");
        } finally {
            if (storePassword != null) Arrays.fill(storePassword, '\0');
            if (keyPassword != null) Arrays.fill(keyPassword, '\0');
            if (!complete && exportStarted && output != null) {
                try {
                    Files.deleteIfExists(output);
                } catch (Exception ignored) {
                    System.err.println("Temporary export cleanup failed");
                }
            }
        }
        if (!complete) System.exit(1);
        System.out.println(report);
    }

    private static void validateZip(Path output, byte[] expectedCertificateDer) throws Exception {
        requireRegularFile(output);
        require(Files.size(output) > 0 && Files.size(output) <= 4L * MAX_ENTRY_BYTES,
                "Unexpected encrypted ZIP size");
        try (ZipFile zip = new ZipFile(output.toFile())) {
            Set<String> actual = new HashSet<>();
            var entries = zip.entries();
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                require(!entry.isDirectory() && EXPECTED_ZIP_ENTRIES.contains(entry.getName()),
                        "Unexpected ZIP entry");
                require(actual.add(entry.getName()), "Duplicate ZIP entry");
            }
            require(actual.equals(EXPECTED_ZIP_ENTRIES), "Incomplete encrypted export");
            byte[] encrypted = readBounded(zip, "encryptedPrivateKey");
            // This console uses an RSA-3072 wrapping key: 384 bytes plus AES-wrapped key.
            require(encrypted.length > 384 && encrypted.length % 8 == 0,
                    "Invalid RSA-AES encrypted key structure");
            byte[] pem = readBounded(zip, "certificate.pem");
            Collection<? extends Certificate> certificates = CertificateFactory.getInstance("X.509")
                    .generateCertificates(new ByteArrayInputStream(pem));
            require(certificates.size() == 1, "Expected exactly one public certificate");
            require(MessageDigest.isEqual(expectedCertificateDer,
                    certificates.iterator().next().getEncoded()), "Exported certificate changed");
        }
    }

    private static byte[] readBounded(ZipFile zip, String entryName) throws Exception {
        try (InputStream input = zip.getInputStream(zip.getEntry(entryName))) {
            byte[] data = input.readNBytes(MAX_ENTRY_BYTES + 1);
            require(data.length > 0 && data.length <= MAX_ENTRY_BYTES, "Invalid ZIP entry size");
            return data;
        }
    }

    private static String requiredEnv(String name) {
        String value = System.getenv(name);
        require(value != null && !value.isEmpty(), "Required signing environment is absent");
        return value;
    }

    private static void requireRegularFile(Path path) {
        require(Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS), "Expected regular file");
    }

    private static String sha256(Path path) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream input = Files.newInputStream(path)) {
            byte[] buffer = new byte[8192];
            int count;
            while ((count = input.read(buffer)) != -1) digest.update(buffer, 0, count);
        }
        return HexFormat.of().formatHex(digest.digest());
    }

    private static String sha256(byte[] bytes) throws Exception {
        return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new IllegalStateException(message);
    }
}
