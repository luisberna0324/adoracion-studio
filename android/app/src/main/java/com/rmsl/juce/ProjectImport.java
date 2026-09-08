package com.rmsl.juce;

import android.content.Context;
import android.net.Uri;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

/** Copies a document-provider URI while its temporary Android grant is valid. */
public final class ProjectImport
{
    private ProjectImport() {}

    public static boolean copyToFile (Context context, String source, String destination)
    {
        final File target = new File (destination);
        final File parent = target.getParentFile();

        if (parent != null && ! parent.exists() && ! parent.mkdirs())
            return false;

        try (InputStream input = context.getContentResolver().openInputStream (Uri.parse (source));
             FileOutputStream output = new FileOutputStream (target))
        {
            if (input == null)
                return false;

            final byte[] buffer = new byte[256 * 1024];
            int count;
            while ((count = input.read (buffer)) != -1)
                output.write (buffer, 0, count);

            output.getFD().sync();
            return true;
        }
        catch (Exception ignored)
        {
            target.delete();
            return false;
        }
    }
}
