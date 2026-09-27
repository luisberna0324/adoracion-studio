# JUCE restarts the iOS first responder on every TextEditor focus change.
# Generate an iOS-only module copy with continuous input for opted-in form
# fields. Keep the external JUCE checkout intact and fail on upstream drift.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    return()
endif()

function(mtcontrols_configure_ios_form_keyboard)
    set(module "${JUCE_DIR}/modules/juce_gui_basics")
    set(peer_path "${module}/native/juce_UIViewComponentPeer_ios.mm")
    file(READ "${peer_path}" peer)
    string(REPLACE "\r\n" "\n" peer "${peer}")
    set(original [=[void UIViewComponentPeer::textInputRequired (Point<int>, TextInputTarget&)
{
    // We need to restart the text input session so that the keyboard can change types if necessary.
    if ([hiddenTextInput.get() isFirstResponder])
        [hiddenTextInput.get() resignFirstResponder];

    [hiddenTextInput.get() becomeFirstResponder];
}]=])
    set(replacement [=[void UIViewComponentPeer::textInputRequired (Point<int>, TextInputTarget& target)
{
    auto* input = hiddenTextInput.get();
    auto* field = dynamic_cast<Component*> (&target);
    if ([input isFirstResponder] && field != nullptr
        && static_cast<bool> (field->getProperties().getWithDefault ("playworshipContinuousTextInput", false)))
    {
        // The JUCE target has changed, but UIKit can retain its input session.
        // Notify the IME of the new text/selection and refresh keyboard traits
        // (e.g. title -> numeric BPM) without a hide/show cycle.
        if (auto* delegate = [input inputDelegate])
        {
            [delegate textWillChange: input];
            [delegate selectionWillChange: input];
            [delegate textDidChange: input];
            [delegate selectionDidChange: input];
        }
        [input reloadInputViews];
        return;
    }
    if ([input isFirstResponder])
        [input resignFirstResponder];
    [input becomeFirstResponder];
}]=])
    string(FIND "${peer}" "${original}" input_offset)
    if(input_offset EQUAL -1)
        message(FATAL_ERROR "JUCE iOS text input changed; review IOSFormKeyboard.cmake before updating JUCE.")
    endif()
    string(REPLACE "${original}" "${replacement}" peer "${peer}")

    set(keyboard_anchor "- (UIKeyboardType) keyboardType\n{")
    set(form_keyboard [=[- (UIKeyboardType) keyboardType
{
    if (auto* field = dynamic_cast<Component*> ([self getTextInputTarget]))
        if (static_cast<bool> (field->getProperties().getWithDefault ("playworshipContinuousTextInput", false))
            && [self getTextInputTarget]->getKeyboardType() == TextInputTarget::numericKeyboard)
            return UIKeyboardTypeNumberPad;
]=])
    string(FIND "${peer}" "${keyboard_anchor}" keyboard_offset)
    if(keyboard_offset EQUAL -1)
        message(FATAL_ERROR "JUCE iOS keyboard type changed; review IOSFormKeyboard.cmake.")
    endif()
    string(REPLACE "${keyboard_anchor}" "${form_keyboard}" peer "${peer}")

    set(traits_anchor "- (UITextAutocapitalizationType) autocapitalizationType")
    set(return_key [=[- (UIReturnKeyType) returnKeyType
{
    if (auto* field = dynamic_cast<Component*> ([self getTextInputTarget]))
        if (static_cast<bool> (field->getProperties().getWithDefault ("playworshipContinuousTextInput", false)))
            return static_cast<bool> (field->getProperties().getWithDefault ("playworshipReturnKeyNext", false))
                       ? UIReturnKeyNext : UIReturnKeyDone;
    return UIReturnKeyDefault;
}

]=])
    string(FIND "${peer}" "${traits_anchor}" traits_offset)
    if(traits_offset EQUAL -1)
        message(FATAL_ERROR "JUCE iOS keyboard traits changed; review IOSFormKeyboard.cmake.")
    endif()
    string(REPLACE "${traits_anchor}" "${return_key}${traits_anchor}" peer "${peer}")

    set(generated "${CMAKE_CURRENT_BINARY_DIR}/playworship-ios-keyboard")
    file(MAKE_DIRECTORY "${generated}")
    file(CONFIGURE OUTPUT "${generated}/juce_UIViewComponentPeer_ios.mm"
         CONTENT "${peer}" @ONLY)
    file(READ "${module}/juce_gui_basics.cpp" module_source)
    set(peer_include "#include \"native/juce_UIViewComponentPeer_ios.mm\"")
    string(FIND "${module_source}" "${peer_include}" include_offset)
    if(include_offset EQUAL -1)
        message(FATAL_ERROR "JUCE iOS module layout changed; review IOSFormKeyboard.cmake.")
    endif()
    string(REPLACE "${peer_include}"
        "#include \"${generated}/juce_UIViewComponentPeer_ios.mm\""
        module_source "${module_source}")
    file(CONFIGURE OUTPUT "${generated}/juce_gui_basics.mm"
         CONTENT "${module_source}" @ONLY)
    foreach(property INTERFACE_SOURCES INTERFACE_JUCE_MODULE_SOURCES)
        get_target_property(sources juce_gui_basics ${property})
        list(FIND sources "${module}/juce_gui_basics.mm" source_index)
        if(source_index EQUAL -1)
            message(FATAL_ERROR "JUCE iOS source target changed; review IOSFormKeyboard.cmake.")
        endif()
        list(REMOVE_ITEM sources "${module}/juce_gui_basics.mm")
        list(APPEND sources "${generated}/juce_gui_basics.mm")
        set_property(TARGET juce_gui_basics PROPERTY ${property} "${sources}")
    endforeach()
    target_include_directories(juce_gui_basics INTERFACE "${module}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${peer_path}" "${module}/juce_gui_basics.cpp")
endfunction()

mtcontrols_configure_ios_form_keyboard()
