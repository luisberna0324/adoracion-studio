#!/usr/bin/env python3
"""Local-only PlayWorship builds. No CI dispatch, provisioning updates or publication."""
import argparse, datetime, hashlib, json, os, platform, plistlib, re, shutil, subprocess, sys, time, zipfile
from pathlib import Path

TARGETS = ['semver', 'resources', 'backend', 'windows', 'android-debug', 'android-release', 'tests', 'macos', 'ios-simulator', 'ios-device']
class Blocked(Exception): pass
class Failed(Exception): pass

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--config', type=Path)
    ap.add_argument('--targets', default='all', help='all or comma-separated '+','.join(TARGETS))
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--semver-branch', help='Source branch for local SemVer validation; defaults to checkout branch')
    ap.add_argument('--jobs', type=int, default=3)
    args = ap.parse_args()
    cfg = json.loads(args.config.read_text()) if args.config else {}
    forbidden = [k for k in cfg if re.search(r'password|secret|token|private.key|keystore', k, re.I)]
    if forbidden: ap.error('Config must not contain credentials or keystore settings.')
    repo = Path(__file__).resolve().parents[1]
    def git(root, *a): return subprocess.check_output(['git', '-C', str(root), *a], text=True).strip()
    sha = git(repo, 'rev-parse', 'HEAD')
    if platform.system() != 'Darwin': ap.error('This pipeline requires the existing macOS toolchain; Windows is reported separately.')
    version = str(cfg.get('version', '1.1.4'))
    if not re.fullmatch(r'\d+\.\d+\.\d+', version): ap.error('version must be MAJOR.MINOR.PATCH')
    jobs = max(1, min(args.jobs, 3))
    selected = TARGETS if args.targets == 'all' else args.targets.split(',')
    if set(selected)-set(TARGETS): ap.error('Unknown target')
    def path(key, default=''): return Path(cfg.get(key, os.environ.get(key.upper(), default))).expanduser().resolve()
    juce, bungee = path('juce_dir', str(repo/'third_party/JUCE')), path('bungee_dir', str(repo/'third_party/bungee'))
    backend = path('backend', str(repo.parent/'backend-main-local'))
    sdk = path('android_sdk', str(Path.home()/'Library/Android/sdk'))
    out = repo/'dist'/'local-pipeline'/f'{version}-{sha[:12]}'/datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    env = {k:v for k,v in os.environ.items() if not re.search(r'PASSWORD|TOKEN|SECRET|CERT|PRIVATE|DSN|API_KEY|KEYSTORE|FIREBASE|GOOGLE_|APPLE_',k)}
    env.pop('CI', None); env['CMAKE_BUILD_PARALLEL_LEVEL']=str(jobs)
    env['NEXT_TELEMETRY_DISABLED']='1'; env['SENTRY_AUTH_TOKEN']=''
    result={'version':version,'app_commit':sha,'app_dirty':bool(git(repo,'status','--porcelain')),'jobs':jobs,'publication':False,'targets':{}}
    if args.dry_run:
        print(json.dumps({'mode':'dry-run','platform':platform.system(),'targets':selected,'output':str(out),'juce_present':(juce/'CMakeLists.txt').exists(),'bungee_present':(bungee/'include/bungee/Bungee.h').exists(),'backend_present':(backend/'package.json').exists(),'ios_manual_signing_supplied':all(cfg.get(k) for k in ['ios_team','ios_profile','ios_identity']),'android_sdk_present':sdk.exists(),'jobs':jobs,'no_commands_executed':True},indent=2)); return 0
    out.mkdir(parents=True)
    def summary():
        (out/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
        (out/'summary.txt').write_text('\n'.join(f'{t}: {v["status"]} — {v.get("detail", "")}' for t,v in result['targets'].items())+'\n')
    def need(file, why):
        if not file.exists(): raise Blocked(why)
    def tool(name):
        if not shutil.which(name): raise Blocked('Missing existing tool: '+name)
    def run(argv, cwd=repo, extra=None):
        for part in argv:
            if part in ['--allowProvisioningUpdates','-allowProvisioningUpdates']: raise Failed('Provisioning changes forbidden')
        with (out/(current+'.log')).open('a') as log:
            log.write('\n$ '+ ' '.join(map(str,argv))+'\n');log.flush()
            completed=subprocess.run(list(map(str,argv)),cwd=cwd,env={**env,**(extra or {})},stdout=log,stderr=subprocess.STDOUT)
        if completed.returncode:
            if current == 'android-debug':
                tail=(out/(current+'.log')).read_text(errors='replace')[-16000:].lower()
                if any(term in tail for term in ['no cached version','offline mode','license not accepted','licenses have not been accepted','failed to find build tools']):
                    raise Blocked('Android cached dependency/SDK/license missing; no installation or license acceptance attempted; see android-debug.log')
            raise Failed(f'Command failed ({completed.returncode}); see {current}.log')
    def common():
        tool('cmake'); need(juce/'CMakeLists.txt','JUCE_DIR must point to existing JUCE checkout');need(bungee/'include/bungee/Bungee.h','Bungee SDK missing; pipeline never runs destructive bootstrap')
        return [f'-DJUCE_DIR={juce}',f'-DMTCONTROLS_BUNGEE_DIR={bungee}','-DMTCONTROLS_ENABLE_SENTRY=OFF',f'-DMTCONTROLS_APP_VERSION={version}','-DMTCONTROLS_BACKEND_MODE=cloud']
    def cmake_build(build, target, config=None, native=()):
        command=['cmake','--build',build,'--target',target,'--parallel',str(jobs)]
        if config: command+=['--config',config]
        if native: command+=['--',*native]
        run(command)
    def artifact(src, name):
        need(src,'Expected artifact missing: '+src.name)
        dest=out/name
        if src.is_dir(): shutil.copytree(src,dest,symlinks=True)
        else: shutil.copy2(src,dest)
        if dest.is_file(): result['targets'][current].setdefault('sha256',{})[name]=hashlib.sha256(dest.read_bytes()).hexdigest()
        result['targets'][current].setdefault('artifacts',[]).append(str(dest))
    for current in selected:
        started=time.monotonic();result['targets'][current]={'status':'RUNNING'};summary();print(current+': RUNNING',flush=True)
        try:
            if current=='semver':
                branch=args.semver_branch or git(repo,'rev-parse','--abbrev-ref','HEAD')
                run(['bash','scripts/semver.sh','--branch',branch,'--sequence','0','--prerelease','--require-supported-branch'])
                result['targets'][current]['branch']=branch
            if current=='resources':
                tool('cmake');tool('ninja');build=repo/'build/binary-resources'
                run(['cmake','-S','tests/binary_resources','-B',build,'-G','Ninja'])
                cmake_build(build,'binary_resources')
                run(['ctest','--test-dir',build,'--output-on-failure','--no-tests=error'])
            if current=='windows': raise Blocked('UNSUPPORTED on this Mac: no Windows Visual Studio 2022/SDK/Inno Setup. No VM or tools installed.')
            if current=='android-release': raise Blocked('Release signing not executed: keystore use needs specific authorization; no key values inspected or generated.')
            if current=='backend':
                tool('node');tool('npm');need(backend/'node_modules','Existing backend dependencies missing; install separately from approved registry')
                if git(backend,'status','--porcelain'): raise Blocked('Backend checkout has changes; use clean worktree to preserve personal source')
                result['backend_commit']=git(backend,'rev-parse','HEAD')
                for name in ['.env','.env.local','.env.production','.env.production.local']:
                    if (backend/name).exists(): raise Blocked('Private dotenv file present; use a clean build checkout without credentials')
                run(['npm','run','typecheck'],backend)
                tests=['paddle-cloud','secret-cache','teams','cloud-catalog','teams-email','auth-callback','google-state','password-auth','apple-auth','device-restrictions']
                run(['node','--import','tsx','--test',*[f'tests/{t}.test.ts' for t in tests]],backend)
                next_env=backend/'next-env.d.ts'; original_next_env=next_env.read_bytes()
                try: run(['npm','run','build'],backend)
                finally: next_env.write_bytes(original_next_env)
                standalone=backend/'.next/standalone'
                if list(standalone.rglob('.env*')): raise Blocked('Standalone contains environment files; refuse packaging')
                name=f'backend-Production-{version}-{result["backend_commit"][:12]}'
                artifact(standalone,name)
                packaged=out/name
                shutil.copytree(backend/'.next/static',packaged/'.next/static',dirs_exist_ok=True)
                if (backend/'public').exists(): shutil.copytree(backend/'public',packaged/'public',dirs_exist_ok=True)
                (packaged/'local-build-metadata.json').write_text(json.dumps({'backend_commit':result['backend_commit'],'version':version,'credentials_embedded':False},indent=2)+'\n')
            if current=='tests':
                run([sys.executable,'tests/test_local_pipeline.py'])
                build=repo/'build/local-merge-tests';run(['cmake','-S',repo,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Debug','-DCMAKE_OSX_ARCHITECTURES=arm64','-DMTCONTROLS_BUILD_TESTS=ON',*common()])
                cmake_build(build,'PlayWorshipTests');run(['ctest','--test-dir',build,'--output-on-failure','--no-tests=error','-LE','interactive|lan'])
            if current=='macos':
                tool('pkgbuild');run(['cmake','--preset','macos-release',*common()]);build=repo/'build/macos-release';cmake_build(build,'MTControls_Standalone')
                app=build/'MTControls_artefacts/Release/Standalone/PlayWorship.app';run(['codesign','--verify','--deep','--strict',app]);run(['lipo',app/'Contents/MacOS/PlayWorship','-verify_arch','arm64','x86_64'])
                artifact(app,f'macos-Release-{version}-{sha[:12]}.app')
                packages=out/'macos-packages';run(['bash','scripts/package_macos_pkg.sh','--version',version,'--standalone-dir',app.parent,'--output-dir',packages])
                result['targets'][current]['artifacts'].append(str(packages/f'playworship-macos-installer-{version}.pkg'))
                result['targets'][current]['distribution']='Local pkg only; not notarized, no certified distribution claim; Apple opt-in OFF/source production URL'
            if current in ['ios-simulator','ios-device']:
                tool('xcodebuild');device=current=='ios-device';build=repo/'build'/('local-pipeline-ios-device' if device else 'local-pipeline-ios-simulator')
                extra=['-DCMAKE_SYSTEM_NAME=iOS','-DCMAKE_OSX_ARCHITECTURES=arm64','-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0',f'-DCMAKE_OSX_SYSROOT={"iphoneos" if device else "iphonesimulator"}','-DMTCONTROLS_APPLE_SIGN_IN_ENABLED=ON',f'-DMTCONTROLS_IOS_BUILD_NUMBER={cfg.get("ios_build_number",4)}']
                signing=[]
                if device:
                    if not all(cfg.get(k) for k in ['ios_team','ios_profile','ios_identity']): raise Blocked('Supply existing authorized development team/profile/identity; no automatic provisioning')
                    extra += [f'-DMTCONTROLS_IOS_DEVELOPMENT_TEAM={cfg["ios_team"]}','-DMTCONTROLS_BACKEND_MODE=custom',f'-DMTCONTROLS_API_BASE_URL={cfg.get("ios_development_api", "https://apple-auth-preview---playworship-yumomf2xoq-ue.a.run.app")}']
                    signing=['CODE_SIGN_STYLE=Manual',f'DEVELOPMENT_TEAM={cfg["ios_team"]}',f'PROVISIONING_PROFILE_SPECIFIER={cfg["ios_profile"]}',f'CODE_SIGN_IDENTITY={cfg["ios_identity"]}']
                else: signing=['CODE_SIGNING_ALLOWED=NO','CODE_SIGNING_REQUIRED=NO']
                run(['cmake','-S',repo,'-B',build,'-G','Xcode',*common(),*extra]);cmake_build(build,'MTControls_Standalone','Debug',signing)
                app=build/'MTControls_artefacts/Debug/Standalone/PlayWorship.app'
                if device:
                    run(['codesign','--verify','--deep','--strict',app])
                    def captured(argv): return subprocess.check_output(list(map(str,argv)),env=env,stderr=subprocess.DEVNULL)
                    ent=plistlib.loads(captured(['codesign','-d','--entitlements',':-',app]))
                    profile=plistlib.loads(captured(['security','cms','-D','-i',app/'embedded.mobileprovision']))
                    if ent.get('com.apple.developer.applesignin') != ['Default'] or profile.get('Entitlements',{}).get('com.apple.developer.applesignin') != ['Default'] or cfg['ios_team'] not in profile.get('TeamIdentifier',[]):
                        raise Failed('Signed app/profile Apple entitlement or team mismatch')
                    result['targets'][current]['apple_entitlement_verified']=True
                artifact(app,f'{current}-Debug-{version}-{sha[:12]}.app')
                result['targets'][current]['distribution']='Development only, no device install/account login/App Store export; simulator unsigned' if not device else 'Existing development signature; preview backend; no installation or App Store export'
            if current=='android-debug':
                tool('java');need(sdk/'platforms/android-36','Existing Android SDK36 required');need(sdk/'ndk/28.1.13356709','Existing NDK28.1 required');need(sdk/'cmake/3.22.1','Existing SDK CMake3.22.1 required');common()
                # Native Android expects these paths. Use only new ignored symlinks; never replace a dependency checkout.
                for dest,source in [(repo/'third_party/JUCE',juce),(repo/'third_party/bungee',bungee)]:
                    if not dest.exists(): dest.parent.mkdir(exist_ok=True);dest.symlink_to(source,target_is_directory=True)
                extras={'ANDROID_SDK_ROOT':str(sdk),'ANDROID_HOME':str(sdk),'PLAYWORSHIP_VERSION_NAME':version,'PLAYWORSHIP_VERSION_CODE':str(cfg.get('android_version_code',4))}
                if cfg.get('java_home'): extras['JAVA_HOME']=cfg['java_home']
                run(['./gradlew',':app:assembleDebug','--offline','--no-daemon',f'--max-workers={jobs}','-Pandroid.builder.sdkDownload=false'],repo/'android',extras)
                apk=repo/'android/app/build/outputs/apk/debug/app-debug.apk'
                need(apk,'APK missing')
                names=set(zipfile.ZipFile(apk).namelist())
                if not {'lib/arm64-v8a/libjuce_jni.so','lib/arm64-v8a/libbungee.so'} <= names: raise Failed('APK missing required arm64 libraries')
                signers=sorted((sdk/'build-tools').glob('*/apksigner'))
                if not signers: raise Blocked('Existing apksigner needed for APK verification')
                run([signers[-1],'verify',apk],extra=extras)
                artifact(apk,f'android-arm64-Debug-{version}-{sha[:12]}.apk')
            result['targets'][current]['status']='PASS'
        except Blocked as error: result['targets'][current].update(status='BLOCKED',detail=str(error))
        except (Failed, OSError, subprocess.SubprocessError) as error: result['targets'][current].update(status='FAIL',detail=str(error))
        except KeyboardInterrupt:
            result['targets'][current].update(status='BLOCKED',detail='Interrupted by user');summary();return 130
        except Exception as error:
            result['targets'][current].update(status='FAIL',detail='Unexpected '+type(error).__name__+'; independent targets continue')
        result['targets'][current]['seconds']=round(time.monotonic()-started,2);summary();print(current+': '+result['targets'][current]['status']+' '+result['targets'][current].get('detail',''),flush=True)
    print('Summary: '+str(out/'summary.json'),flush=True)
    return 1 if any(v['status']=='FAIL' for v in result['targets'].values()) else (2 if any(v['status']=='BLOCKED' for v in result['targets'].values()) else 0)
if __name__=='__main__': sys.exit(main())
