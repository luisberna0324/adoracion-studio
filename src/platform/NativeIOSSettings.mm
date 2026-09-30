#import <UIKit/UIKit.h>

#include "NativeIOSSettings.h"
#include "../ui/panels/SettingsPanel.h"
#include "../audio/OutputChannelCatalog.h"
#include "../data/Localization.h"

enum Page { home, appearance, playback, library, routing, route, remote, account, diagnostics, choicePage };
enum Choice { theme, language, crossfade, click, device, server, width, channel };
using Mode = playworship::remote::RemoteSession::Mode;

NSString *str(const juce::String &value) { return [NSString stringWithUTF8String:value.toRawUTF8()]; }
NSString *tr(const char *es, const char *en) { return str(L10n::tr(es, en)); }

NSString *displayPlan(const juce::String &value) {
  if (value.equalsIgnoreCase("annual")) return tr("Anual", "Annual");
  if (value.equalsIgnoreCase("monthly")) return tr("Mensual", "Monthly");
  if (value.equalsIgnoreCase("free")) return tr("Gratis", "Free");
  return str(value);
}

NSString *displayStatus(const juce::String &value) {
  if (value.equalsIgnoreCase("active")) return tr("Activa", "Active");
  if (value.equalsIgnoreCase("trialing")) return tr("En prueba", "Trialing");
  if (value.equalsIgnoreCase("past_due")) return tr("Pago pendiente", "Past due");
  if (value.equalsIgnoreCase("paused")) return tr("Pausada", "Paused");
  if (value.equalsIgnoreCase("canceled")) return tr("Cancelada", "Canceled");
  return str(value);
}

NSString *displayExpiry(const juce::String &value, int languageId) {
  NSISO8601DateFormatter *parser = [[NSISO8601DateFormatter alloc] init];
  parser.formatOptions = NSISO8601DateFormatWithInternetDateTime | NSISO8601DateFormatWithFractionalSeconds;
  NSDate *date = [parser dateFromString:str(value)];
  if (!date) {
    parser.formatOptions = NSISO8601DateFormatWithInternetDateTime;
    date = [parser dateFromString:str(value)];
  }
  if (!date) return str(value);
  NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
  formatter.locale = [NSLocale localeWithLocaleIdentifier:languageId == 2 ? @"en_US" : @"es_ES"];
  formatter.dateStyle = NSDateFormatterMediumStyle;
  formatter.timeStyle = NSDateFormatterShortStyle;
  return [formatter stringFromDate:date];
}

UIViewController *activeController() {
  UIWindow *fallback = nil;
  for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
    if (![scene isKindOfClass:UIWindowScene.class]) continue;
    for (UIWindow *window in ((UIWindowScene *)scene).windows) {
      if (window.rootViewController && !fallback) fallback = window;
      if (!window.isKeyWindow) continue;
      UIViewController *controller = window.rootViewController;
      while (controller.presentedViewController && !controller.presentedViewController.isBeingDismissed)
        controller = controller.presentedViewController;
      return controller;
    }
  }
  if (fallback) return fallback.rootViewController;
  return UIApplication.sharedApplication.keyWindow.rootViewController;
}

static UINavigationController *settingsNavigation;
static std::function<void()> closeSettings;

NSInteger categoryAtRow(const SettingsPanel::IOSSettingsState &state, NSInteger row) {
  NSInteger clickIndex = -1, cuesIndex = -1;
  for (int i = 0; i < state.categories.size(); ++i) {
    if (state.categories[i].equalsIgnoreCase("Click")) clickIndex = i;
    if (state.categories[i].equalsIgnoreCase("Cues")) cuesIndex = i;
  }
  if (clickIndex >= 0 && row-- == 0) return clickIndex;
  if (cuesIndex >= 0 && row-- == 0) return cuesIndex;
  for (int i = 0; i < state.categories.size(); ++i) {
    if (i == clickIndex || i == cuesIndex) continue;
    if (row-- == 0) return i;
  }
  return 0;
}

@interface PWSettingsController : UITableViewController <UITextFieldDelegate, UIAdaptivePresentationControllerDelegate> {
@public
  SettingsPanel *panel;
  Page page;
  Choice choice;
  NSInteger category;
  BOOL editingAlias;
}
- (instancetype)initWithPanel:(SettingsPanel *)p page:(Page)next category:(NSInteger)index choice:(Choice)kind;
- (void)refresh;
@end

@implementation PWSettingsController

- (instancetype)initWithPanel:(SettingsPanel *)p page:(Page)next category:(NSInteger)index choice:(Choice)kind {
  self = [super initWithStyle:UITableViewStyleInsetGrouped];
  if (self) { panel = p; page = next; category = index; choice = kind; }
  return self;
}

- (void)viewDidLoad {
  [super viewDidLoad];
  self.tableView.backgroundColor = UIColor.systemGroupedBackgroundColor;
  self.tableView.rowHeight = UITableViewAutomaticDimension;
  self.tableView.estimatedRowHeight = 64;
  self.navigationController.navigationBar.prefersLargeTitles = YES;
  self.navigationItem.largeTitleDisplayMode = page == home ? UINavigationItemLargeTitleDisplayModeAlways
                                                            : UINavigationItemLargeTitleDisplayModeNever;
  self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc]
      initWithBarButtonSystemItem:UIBarButtonSystemItemClose target:self action:@selector(closeTapped)];
  [self updateTitle];
}

- (void)viewWillAppear:(BOOL)animated {
  [super viewWillAppear:animated];
  [self refresh];
}


- (void)updateTitle {
  auto state = panel->iosState();
  switch (page) {
    case home: self.title = tr(u8"Configuración", "Settings"); break;
    case appearance: self.title = tr("Apariencia e idioma", "Appearance & language"); break;
    case playback: self.title = tr(u8"Reproducción", "Playback"); break;
    case library: self.title = tr("Biblioteca", "Library"); break;
    case routing: self.title = tr("Ruteo de salidas", "Output routing"); break;
    case route: self.title = category >= 0 && category < state.categories.size()
        ? str(state.categories[(int)category]) : tr("Ruteo", "Routing"); break;
    case remote: self.title = tr("Control remoto", "Remote control"); break;
    case account: self.title = tr("Mi cuenta", "My account"); break;
    case diagnostics: self.title = tr(u8"Diagnóstico", "Diagnostics"); break;
    case choicePage: {
      const char *es[] = {"Tema", "Idioma", "Crossfade de pads", "Estilo del click", "Salida de audio", "Servidor", "Formato", "Canal"};
      const char *en[] = {"Theme", "Language", "Pads crossfade", "Click style", "Audio output", "Server", "Format", "Channel"};
      self.title = tr(es[choice], en[choice]);
      break;
    }
  }
}

- (void)refresh {
  if (!self.isViewLoaded || editingAlias || panel == nullptr) return;
  [self updateTitle];
  [self.tableView reloadData];
}

- (void)closeTapped { if (closeSettings) { auto callback = closeSettings; callback(); } }

- (void)presentationControllerDidDismiss:(UIPresentationController *)presentationController {
  if (closeSettings) { auto callback = closeSettings; callback(); }
}

- (void)push:(Page)next category:(NSInteger)index choice:(Choice)kind {
  [self.navigationController pushViewController:[[PWSettingsController alloc]
      initWithPanel:panel page:next category:index choice:kind] animated:YES];
}

- (NSArray<NSString *> *)choices:(SettingsPanel::IOSSettingsState const &)state {
  switch (choice) {
    case theme: return @[tr("Verde Noche", "Night Green"), tr(u8"Ámbar Estudio", "Studio Amber")];
    case language: return @[@"Español", @"English"];
    case crossfade: return @[tr(u8"Rápido (1 s)", "Fast (1 s)"), tr("Medio (3 s)", "Medium (3 s)"), tr("Largo (5 s)", "Long (5 s)")];
    case click: return @[@"Classic", @"Woodblock", @"Cowbell", @"Digital", @"Gentle", @"Percussive", @"Saw", @"Blip"];
    case device: {
      NSMutableArray<NSString *> *items = [NSMutableArray array];
      for (auto &name : state.devices) [items addObject:str(name)];
      return items;
    }
    case server: {
      NSMutableArray<NSString *> *items = [NSMutableArray array];
      for (auto &item : state.servers) [items addObject:str(item.instance + " (" + item.address + ")")];
      return items;
    }
    case width: return @[@"Mono", tr(u8"Estéreo", "Stereo")];
    case channel: {
      if (category < 0 || category >= (NSInteger)state.routing.size()) return @[];
      const auto names = state.routing[(size_t)category].width >= 2
          ? OutputChannelCatalog::buildStereoPairNames(state.outputChannels)
          : OutputChannelCatalog::buildOutputChannelNames(state.outputChannels);
      NSMutableArray<NSString *> *items = [NSMutableArray array];
      for (auto &name : names) [items addObject:str(name)];
      return items;
    }
  }
  return @[];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
  switch (page) {
    case home: return 3;
    case appearance: return 1;
    case playback: return 1;
    case library: return 1;
    case routing: return 2;
    case route: return 1;
    case remote: return 2;
    case account: return 3;
    case diagnostics: return 1;
    case choicePage: return 1;
  }
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
  auto state = panel->iosState();
  switch (page) {
    case home: return section == 0 ? 3 : 2;
    case appearance: return 2;
    case playback: return 2;
    case library: return 2;
    case routing: return section == 0 ? 1 : state.categories.size();
    case route: return 2;
    case remote: return section == 0 ? 2 : (state.remoteMode == Mode::Local ? 1 : 2);
    case account: return section == 0 ? 7 : section == 1 ? 3 : 3;
    case diagnostics: return 1;
    case choicePage: return [self choices:state].count;
  }
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
  switch (page) {
    case home: return section == 0 ? tr(u8"Personalización", "Personalization")
        : section == 1 ? tr("Audio y conexiones", "Audio & connections") : tr("Cuenta y soporte", "Account & support");
    case routing: return section == 0 ? tr("Dispositivo", "Device") : tr(u8"Categorías", "Categories");
    case remote: return section == 0 ? tr("Modo", "Mode") : tr("Estado y conexión", "Status & connection");
    case account: return section == 0 ? tr(u8"Sesión", "Session") : section == 1 ? tr("Cuotas", "Quotas") : tr("Acciones", "Actions");
    default: return nil;
  }
}

- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
  if (page == routing && section == 0)
    return tr("Las asignaciones se guardan por dispositivo.", "Routing is saved per device.");
  if (page == routing && section == 1)
    return tr("Mono usa una salida; estéreo, dos consecutivas. Canal elige la primera.",
              "Mono uses one output; stereo uses two adjacent outputs. Channel selects the first.");
  if (page == diagnostics)
    return tr("Incluye un identificador de cuenta y datos del dispositivo. No incluye audio, credenciales, correos, nombres de canciones ni rutas locales.",
              "Includes an account identifier and device data. No audio, credentials, email addresses, song names, or local paths.");
  return nil;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)path {
  auto state = panel->iosState();
  auto *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
  cell.textLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
  cell.detailTextLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
  cell.detailTextLabel.textColor = UIColor.secondaryLabelColor;
  cell.detailTextLabel.numberOfLines = 0;
  cell.textLabel.adjustsFontForContentSizeCategory = YES;
  cell.detailTextLabel.adjustsFontForContentSizeCategory = YES;
  cell.selectionStyle = UITableViewCellSelectionStyleDefault;
  NSInteger row = path.row, section = path.section;

  if (page == home) {
    static const Page destinations[] = {appearance, playback, library, routing, remote, account, diagnostics};
    NSInteger index = section == 0 ? row : section == 1 ? row + 3 : row + 5;
    const char *es[] = {"Apariencia e idioma", u8"Reproducción", "Biblioteca", "Ruteo de salidas", "Control remoto", "Mi cuenta", u8"Diagnóstico"};
    const char *en[] = {"Appearance & language", "Playback", "Library", "Output routing", "Remote control", "My account", "Diagnostics"};
    const char *subEs[] = {"Tema e idioma", "Click y transiciones de pads", "Importación y almacenamiento", "Dispositivo y canales", "Conexiones en red local", "Plan y almacenamiento", "Informe para soporte"};
    const char *subEn[] = {"Theme and language", "Click and pad transitions", "Import and storage", "Device and channels", "Local network connections", "Plan and storage", "Report for support"};
    cell.textLabel.text = tr(es[index], en[index]);
    cell.detailTextLabel.text = index == 5 && state.session.email.isNotEmpty() ? str(state.session.email) : tr(subEs[index], subEn[index]);
    cell.imageView.image = [UIImage systemImageNamed:@[@"paintbrush", @"play.circle", @"books.vertical", @"cable.connector", @"wifi", @"person.crop.circle", @"waveform.path.ecg"][index]];
    cell.imageView.tintColor = UIColor.systemGreenColor;
    cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    (void)destinations;
  } else if (page == appearance || page == playback) {
    bool app = page == appearance;
    cell.textLabel.text = app ? (row == 0 ? tr("Tema", "Theme") : tr("Idioma", "Language"))
        : (row == 0 ? tr("Crossfade de pads", "Pads crossfade") : tr("Estilo del click", "Click style"));
    NSString *help = app ? (row == 0 ? tr("Elige los colores de la aplicación.", "Choose the app colours.")
        : tr("Se aplica a menús, controles y mensajes.", "Applies to menus, controls, and messages."))
        : (row == 0 ? tr("Duración del cambio suave entre pads.", "Duration of the smooth transition between pads.")
        : tr("Cambia el sonido del click sin alterar el tempo.", "Changes the click sound without changing tempo."));
    Choice kind = app ? (row == 0 ? theme : language) : (row == 0 ? crossfade : click);
    Choice old = choice;
    choice = kind;
    NSArray<NSString *> *items = [self choices:state];
    choice = old;
    NSInteger value = app ? (row == 0 ? state.theme - 2 : state.language - 1)
        : (row == 0 ? state.padsCrossfade - 1 : state.clickStyle - 1);
    cell.detailTextLabel.text = value >= 0 && value < (NSInteger)items.count
        ? [NSString stringWithFormat:@"%@ · %@", items[(NSUInteger)value], help] : help;
    cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
  } else if (page == library) {
    if (row == 0) {
      cell.textLabel.text = tr("Importar desde Prime", "Import from Prime");
      cell.detailTextLabel.text = tr("Disponible solo en macOS para la biblioteca local de Prime.",
                                     "Available only on macOS for the local Prime library.");
      cell.textLabel.textColor = UIColor.tertiaryLabelColor;
      cell.userInteractionEnabled = NO;
    } else {
      cell.textLabel.text = tr(u8"Optimización de stems", "Stem optimization");
      cell.detailTextLabel.text = tr("Reduce el espacio de los stems en importaciones compatibles.",
                                     "Reduces stem storage in supported imports.");
      UISwitch *toggle = [[UISwitch alloc] init];
      toggle.on = state.stemOptimization;
      [toggle addTarget:self action:@selector(stemChanged:) forControlEvents:UIControlEventValueChanged];
      cell.accessoryView = toggle;
    }
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
  } else if (page == routing) {
    if (section == 0) {
      cell.textLabel.text = tr("Salida de audio", "Audio output");
      cell.detailTextLabel.text = str(state.deviceName);
    } else {
      NSInteger index = categoryAtRow(state, row);
      cell.textLabel.text = str(state.categories[(int)index]);
      if (index < (NSInteger)state.routing.size()) {
        const auto &value = state.routing[(size_t)index];
        cell.detailTextLabel.text = [NSString stringWithFormat:@"%@ · %@ %d", value.width >= 2 ? tr(u8"Estéreo", "Stereo") : @"Mono", tr("Canal", "Channel"), value.baseChannel];
      }
    }
    cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
  } else if (page == route) {
    auto value = state.routing[(size_t)category];
    cell.textLabel.text = row == 0 ? tr("Formato", "Format") : tr("Canal de salida", "Output channel");
    cell.detailTextLabel.text = row == 0 ? (value.width >= 2 ? tr(u8"Estéreo", "Stereo") : @"Mono")
        : [NSString stringWithFormat:@"%d", value.baseChannel];
    cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
  } else if (page == remote) {
    if (section == 0 && row == 0) {
      cell.textLabel.text = tr("Control remoto", "Remote control");
      cell.detailTextLabel.text = tr("Local reproduce aquí; Cliente controla un servidor; Servidor comparte el control.",
                                     "Local plays here; Client controls a server; Server shares control.");
      cell.selectionStyle = UITableViewCellSelectionStyleNone;
    } else if (section == 0) {
      UISegmentedControl *segments = [[UISegmentedControl alloc] initWithItems:@[@"Local", tr("Cliente", "Client"), tr("Servidor", "Server")]];
      segments.selectedSegmentIndex = state.remoteMode == Mode::Client ? 1
          : state.remoteMode == Mode::Server ? 2 : 0;
      segments.translatesAutoresizingMaskIntoConstraints = NO;
      [segments addTarget:self action:@selector(modeChanged:) forControlEvents:UIControlEventValueChanged];
      [cell.contentView addSubview:segments];
      [NSLayoutConstraint activateConstraints:@[
        [segments.leadingAnchor constraintEqualToAnchor:cell.contentView.leadingAnchor constant:16],
        [segments.trailingAnchor constraintEqualToAnchor:cell.contentView.trailingAnchor constant:-16],
        [segments.centerYAnchor constraintEqualToAnchor:cell.contentView.centerYAnchor],
        [segments.heightAnchor constraintEqualToConstant:34]
      ]];
      cell.selectionStyle = UITableViewCellSelectionStyleNone;
    } else if (row == 0) {
      cell.textLabel.text = tr("Estado", "Status");
      cell.detailTextLabel.text = str(state.remoteStatus);
      cell.selectionStyle = UITableViewCellSelectionStyleNone;
    } else if (state.remoteMode == Mode::Client) {
      cell.textLabel.text = tr("Servidor", "Server");
      cell.detailTextLabel.text = state.selectedServer >= 0 && state.selectedServer < (int)state.servers.size()
          ? str(state.servers[(size_t)state.selectedServer].instance)
          : tr("Buscar servidor…", "Find server…");
      cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    } else {
      UITextField *field = [[UITextField alloc] init];
      field.translatesAutoresizingMaskIntoConstraints = NO;
      field.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
      field.adjustsFontForContentSizeCategory = YES;
      field.placeholder = tr("Nombre visible para clientes", "Name shown to clients");
      field.text = str(state.remoteAlias);
      field.clearButtonMode = UITextFieldViewModeWhileEditing;
      field.returnKeyType = UIReturnKeyDone;
      field.delegate = self;
      [field addTarget:self action:@selector(aliasEnded:) forControlEvents:UIControlEventEditingDidEnd];
      [cell.contentView addSubview:field];
      [NSLayoutConstraint activateConstraints:@[
        [field.leadingAnchor constraintEqualToAnchor:cell.contentView.leadingAnchor constant:16],
        [field.trailingAnchor constraintEqualToAnchor:cell.contentView.trailingAnchor constant:-16],
        [field.centerYAnchor constraintEqualToAnchor:cell.contentView.centerYAnchor],
        [field.heightAnchor constraintEqualToConstant:40]
      ]];
      cell.selectionStyle = UITableViewCellSelectionStyleNone;
    }
  } else if (page == account) {
    if (section == 0) {
      NSString *labels[] = {@"UID", @"Email", tr("Plan", "Plan"), tr("Estado", "Status"), tr("Vence", "Expires"), tr("Licencia", "License"), tr(u8"Versión", "Version")};
      juce::String values[] = {state.session.uid, state.session.email, state.session.plan,
          state.session.subscriptionStatus, state.session.expiresAt, state.session.licenseKey, state.version};
      cell.textLabel.text = labels[row];
      cell.detailTextLabel.text = values[row].isEmpty() ? @"—"
          : row == 2 ? displayPlan(values[row])
          : row == 3 ? displayStatus(values[row])
          : row == 4 ? displayExpiry(values[row], state.language)
          : str(values[row]);
      if (row == 0 || row == 5) {
        UIButton *copy = [UIButton buttonWithType:UIButtonTypeSystem];
        [copy setImage:[UIImage systemImageNamed:@"doc.on.doc"] forState:UIControlStateNormal];
        copy.frame = CGRectMake(0, 0, 44, 44);
        copy.tag = row;
        copy.accessibilityLabel = tr("Copiar", "Copy");
        [copy addTarget:self action:@selector(copyAccountValue:) forControlEvents:UIControlEventTouchUpInside];
        cell.accessoryView = copy;
      }
      else cell.selectionStyle = UITableViewCellSelectionStyleNone;
    } else if (section == 1) {
      NSString *labels[] = {tr("Dispositivos", "Devices"), tr("Canciones sincronizadas", "Synced songs"), tr("Almacenamiento", "Storage")};
      cell.textLabel.text = labels[row];
      if (state.quotaUnavailable) cell.detailTextLabel.text = tr("No disponible", "Unavailable");
      else if (state.session.accountType.isEmpty()) cell.detailTextLabel.text = tr("Consultando…", "Loading…");
      else if (row == 0) cell.detailTextLabel.text = [NSString stringWithFormat:@"%d / %d", state.session.usedDevices, state.session.maxDevices];
      else if (row == 1) cell.detailTextLabel.text = [NSString stringWithFormat:@"%d / %d", state.session.syncedSongCount, state.session.maxSyncedSongs];
      else cell.detailTextLabel.text = [NSString stringWithFormat:@"%.1f / %.1f GB", (double)state.session.usedStorageBytes / 1e9, (double)state.session.maxStorageBytes / 1e9];
      cell.selectionStyle = UITableViewCellSelectionStyleNone;
    } else {
      cell.textLabel.text = row == 0 ? tr("Contactar soporte", "Contact support")
          : row == 1 ? tr(u8"Cerrar sesión", "Sign out") : tr("Purgar datos locales", "Purge local data");
      cell.detailTextLabel.text = row == 0 ? tr("Abre un correo para contactar a soporte.", "Opens an email to contact support.")
          : row == 1 ? tr("Desconecta esta cuenta de la aplicación.", "Disconnects this account from the app.")
          : tr("Vacía la Biblioteca local; conserva los .pwproj y la nube.", "Empties the local Library; keeps .pwproj files and cloud data.");
      if (row > 0) cell.textLabel.textColor = UIColor.systemRedColor;
    }
  } else if (page == diagnostics) {
    cell.textLabel.text = state.diagnosticsBusy ? tr(u8"Preparando…", "Preparing…")
        : tr(u8"Compartir diagnóstico", "Share diagnostics");
    cell.detailTextLabel.text = tr(u8"Genera un informe de los últimos 2 minutos de métricas técnicas.",
                                   "Generate a report with the last 2 minutes of technical metrics.");
    cell.userInteractionEnabled = !state.diagnosticsBusy;
    cell.textLabel.textColor = UIColor.systemGreenColor;
  } else if (page == choicePage) {
    NSArray<NSString *> *items = [self choices:state];
    cell.textLabel.text = items[(NSUInteger)row];
    NSInteger selected = choice == theme ? state.theme - 2 : choice == language ? state.language - 1
        : choice == crossfade ? state.padsCrossfade - 1 : choice == click ? state.clickStyle - 1
        : choice == device ? state.selectedDevice : choice == server ? state.selectedServer
        : choice == width ? state.routing[(size_t)category].width - 1
        : state.routing[(size_t)category].baseChannel - 1;
    cell.accessoryType = selected == row ? UITableViewCellAccessoryCheckmark : UITableViewCellAccessoryNone;
    if (choice == width && row == 1 && state.outputChannels < 2) {
      cell.textLabel.enabled = NO;
      cell.userInteractionEnabled = NO;
    }
  }
  return cell;
}

- (CGFloat)tableView:(UITableView *)tableView heightForRowAtIndexPath:(NSIndexPath *)path {
  if (page == remote && ((path.section == 0 && path.row == 1) ||
      (path.section == 1 && path.row == 1 && panel->iosState().remoteMode == Mode::Server))) return 54;
  return UITableViewAutomaticDimension;
}

- (void)stemChanged:(UISwitch *)sender { panel->iosSetStemOptimization(sender.on); }
- (void)copyAccountValue:(UIButton *)sender {
  auto state = panel->iosState();
  UIPasteboard.generalPasteboard.string = str(sender.tag == 0 ? state.session.uid : state.session.licenseKey);
  UIAccessibilityPostNotification(UIAccessibilityAnnouncementNotification, tr("Copiado", "Copied"));
}
- (void)modeChanged:(UISegmentedControl *)sender {
  panel->iosSetRemoteMode(sender.selectedSegmentIndex == 1 ? Mode::Client
      : sender.selectedSegmentIndex == 2 ? Mode::Server : Mode::Local);
  [self refresh];
}
- (void)aliasEnded:(UITextField *)sender {
  editingAlias = NO;
  panel->iosSetRemoteAlias(juce::String::fromUTF8(sender.text.UTF8String ?: ""));
}
- (BOOL)textFieldShouldReturn:(UITextField *)field { [field resignFirstResponder]; return YES; }
- (void)textFieldDidBeginEditing:(UITextField *)field { editingAlias = YES; }

- (void)tableView:(UITableView *)tableView accessoryButtonTappedForRowWithIndexPath:(NSIndexPath *)path {
  [self tableView:tableView didSelectRowAtIndexPath:path];
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)path {
  [tableView deselectRowAtIndexPath:path animated:YES];
  NSInteger row = path.row, section = path.section;
  auto state = panel->iosState();
  if (page == home) {
    const Page destinations[] = {appearance, playback, library, routing, remote, account, diagnostics};
    NSInteger index = section == 0 ? row : section == 1 ? row + 3 : row + 5;
    [self push:destinations[index] category:-1 choice:theme];
  } else if (page == appearance) {
    [self push:choicePage category:-1 choice:row == 0 ? theme : language];
  } else if (page == playback) {
    [self push:choicePage category:-1 choice:row == 0 ? crossfade : click];
  } else if (page == routing) {
    if (section == 0) [self push:choicePage category:-1 choice:device];
    else [self push:route category:categoryAtRow(state, row) choice:width];
  } else if (page == route) {
    [self push:choicePage category:category choice:row == 0 ? width : channel];
  } else if (page == remote && section == 1 && row == 1 && state.remoteMode == Mode::Client) {
    [self push:choicePage category:-1 choice:server];
  } else if (page == account && section == 0 && (row == 0 || row == 5)) {
    UIPasteboard.generalPasteboard.string = str(row == 0 ? state.session.uid : state.session.licenseKey);
    UIAccessibilityPostNotification(UIAccessibilityAnnouncementNotification, tr("Copiado", "Copied"));
  } else if (page == account && section == 2) {
    if (row == 0) {
      NSURL *url = [NSURL URLWithString:@"mailto:hola@playworship.app"];
      [UIApplication.sharedApplication openURL:url options:@{} completionHandler:nil];
    } else {
      NSString *title = row == 1 ? tr(u8"¿Cerrar sesión?", "Sign out?")
          : tr("Purgar datos locales de Cloud", "Purge local Cloud data");
      NSString *message = row == 1
          ? tr(u8"Tendrás que iniciar sesión nuevamente para acceder a tu biblioteca y a las canciones de Cloud.",
               "You'll need to sign in again to access your library and Cloud songs.")
          : tr("Se eliminarán las canciones y setlists de Biblioteca, cachés, descargas y vínculos de sincronización. Los .pwproj y el contenido de la nube se conservan.",
               "Library songs and setlists, caches, downloads, and sync links will be removed. .pwproj files and cloud content remain.");
      UIAlertController *alert = [UIAlertController alertControllerWithTitle:title message:message preferredStyle:UIAlertControllerStyleAlert];
      [alert addAction:[UIAlertAction actionWithTitle:tr("Cancelar", "Cancel") style:UIAlertActionStyleCancel handler:nil]];
      SettingsPanel *target = panel;
      [alert addAction:[UIAlertAction actionWithTitle:row == 1 ? tr(u8"Cerrar sesión", "Sign out") : tr("Purgar", "Purge")
          style:UIAlertActionStyleDestructive handler:^(UIAlertAction *) {
        if (row == 1) target->iosRequestLogout(); else target->iosRequestPurge();
      }]];
      [self presentViewController:alert animated:YES completion:nil];
    }
  } else if (page == diagnostics) {
    if (!state.diagnosticsBusy) panel->iosShareDiagnostics();
  } else if (page == choicePage) {
    switch (choice) {
      case theme: panel->iosSelectTheme((int)row + 2); break;
      case language: panel->iosSelectLanguage((int)row + 1); break;
      case crossfade: panel->iosSelectPadsCrossfade((int)row + 1); break;
      case click: panel->iosSelectClickStyle((int)row + 1); break;
      case device: panel->iosSelectRoutingDevice((int)row); break;
      case server: panel->iosSelectRemoteServer((int)row); break;
      case width: {
        auto current = state.routing[(size_t)category];
        panel->iosSetRouting((size_t)category, (int)row + 1, current.baseChannel);
        break;
      }
      case channel: {
        auto current = state.routing[(size_t)category];
        panel->iosSetRouting((size_t)category, current.width, (int)row + 1);
        break;
      }
    }
    [self.navigationController popViewControllerAnimated:YES];
    NativeIOSSettings::refresh();
  }
}
@end
namespace NativeIOSSettings {
bool show(SettingsPanel &panel, std::function<void()> onClose) {
  if (settingsNavigation != nil) return false;
  UIViewController *presenter = activeController();
  if (presenter == nil) return false;
  closeSettings = std::move(onClose);
  PWSettingsController *root = [[PWSettingsController alloc] initWithPanel:&panel page:home category:-1 choice:theme];
  settingsNavigation = [[UINavigationController alloc] initWithRootViewController:root];
  settingsNavigation.view.tintColor = UIColor.systemGreenColor;
  settingsNavigation.modalPresentationStyle = UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad
      ? UIModalPresentationFormSheet : UIModalPresentationFullScreen;
  settingsNavigation.presentationController.delegate = root;
  [presenter presentViewController:settingsNavigation animated:YES completion:nil];
  return true;
}

void dismiss() {
  if (settingsNavigation == nil) return;
  UINavigationController *navigation = settingsNavigation;
  settingsNavigation = nil;
  closeSettings = {};
  for (UIViewController *controller in navigation.viewControllers)
    ((PWSettingsController *)controller)->panel = nullptr;
  [navigation dismissViewControllerAnimated:YES completion:nil];
}

void refresh() {
  if (settingsNavigation == nil) return;
  [(PWSettingsController *)settingsNavigation.topViewController refresh];
}
} // namespace NativeIOSSettings
